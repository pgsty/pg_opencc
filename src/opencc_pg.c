/*
 * opencc_pg.c
 *    PostgreSQL interface of the opencc extension.
 *
 * All OpenCC work happens behind opencc_core.h.  This file validates
 * arguments, turns engine error codes into ereport(), and copies results into
 * palloc'd datums.
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "funcapi.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/tuplestore.h"
#include "utils/varlena.h"

#include "opencc_core.h"
#include "opencc_resources.h"

#ifndef OPENCC_PG_VERSION
#define OPENCC_PG_VERSION "unknown"
#endif

#ifdef PG_MODULE_MAGIC_EXT
PG_MODULE_MAGIC_EXT(.name = "opencc", .version = OPENCC_PG_VERSION);
#else
PG_MODULE_MAGIC;
#endif

void		_PG_init(void);

PG_FUNCTION_INFO_V1(opencc_convert);
PG_FUNCTION_INFO_V1(opencc_localize);
PG_FUNCTION_INFO_V1(opencc_detect);
PG_FUNCTION_INFO_V1(opencc_script_stats);
PG_FUNCTION_INFO_V1(opencc_candidates);
PG_FUNCTION_INFO_V1(opencc_explain);
PG_FUNCTION_INFO_V1(opencc_ambiguities);
PG_FUNCTION_INFO_V1(opencc_version);
PG_FUNCTION_INFO_V1(opencc_configs);
PG_FUNCTION_INFO_V1(opencc_routes);

static char *opencc_preload = NULL;

/* ------------------------------------------------------------------------
 * Language regions and the conversion route between each pair
 * ------------------------------------------------------------------------
 */
typedef enum Region
{
	R_HANS,						/* Simplified Chinese script */
	R_HANT,						/* Traditional Chinese script (OpenCC standard) */
	R_CN,						/* Mainland China: simplified script and vocabulary */
	R_TW,						/* Taiwan: traditional script and vocabulary */
	R_HK,						/* Hong Kong: traditional script and vocabulary */
	R_JA,						/* Japanese Shinjitai */
	R_COUNT
} Region;

static const char *const region_names[R_COUNT] = {
	"zh-Hans", "zh-Hant", "zh-CN", "zh-TW", "zh-HK", "ja"
};

typedef struct Route
{
	Region		source;
	Region		target;
	const char *chain[2];		/* configs applied in order; NULL = unused */
} Route;

static const Route routes[] = {
	{R_HANS, R_HANS, {NULL, NULL}}, {R_HANS, R_HANT, {"s2t", NULL}},
	{R_HANS, R_CN, {NULL, NULL}}, {R_HANS, R_TW, {"s2twp", NULL}},
	{R_HANS, R_HK, {"s2hkp", NULL}}, {R_HANS, R_JA, {"s2t", "t2jp"}},

	{R_HANT, R_HANS, {"t2s", NULL}}, {R_HANT, R_HANT, {NULL, NULL}},
	{R_HANT, R_CN, {"t2s", NULL}}, {R_HANT, R_TW, {"t2tw", NULL}},
	{R_HANT, R_HK, {"t2hk", NULL}}, {R_HANT, R_JA, {"t2jp", NULL}},

	{R_CN, R_HANS, {NULL, NULL}}, {R_CN, R_HANT, {"s2t", NULL}},
	{R_CN, R_CN, {NULL, NULL}}, {R_CN, R_TW, {"s2twp", NULL}},
	{R_CN, R_HK, {"s2hkp", NULL}}, {R_CN, R_JA, {"s2t", "t2jp"}},

	{R_TW, R_HANS, {"tw2s", NULL}}, {R_TW, R_HANT, {"tw2t", NULL}},
	{R_TW, R_CN, {"tw2sp", NULL}}, {R_TW, R_TW, {NULL, NULL}},
	{R_TW, R_HK, {"tw2t", "t2hk"}}, {R_TW, R_JA, {"tw2t", "t2jp"}},

	{R_HK, R_HANS, {"hk2s", NULL}}, {R_HK, R_HANT, {"hk2t", NULL}},
	{R_HK, R_CN, {"hk2sp", NULL}}, {R_HK, R_TW, {"hk2t", "t2tw"}},
	{R_HK, R_HK, {NULL, NULL}}, {R_HK, R_JA, {"hk2t", "t2jp"}},

	{R_JA, R_HANS, {"jp2t", "t2s"}}, {R_JA, R_HANT, {"jp2t", NULL}},
	{R_JA, R_CN, {"jp2t", "t2s"}}, {R_JA, R_TW, {"jp2t", "t2tw"}},
	{R_JA, R_HK, {"jp2t", "t2hk"}}, {R_JA, R_JA, {NULL, NULL}},
};

/* ------------------------------------------------------------------------
 * helpers
 * ------------------------------------------------------------------------
 */
static void
check_encoding(void)
{
	if (GetDatabaseEncoding() != PG_UTF8)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("opencc requires a UTF8 database"),
				 errdetail("The current database encoding is %s.",
						   GetDatabaseEncodingName())));
}

static void
engine_error(int rc, const char *what)
{
	if (rc == OCPG_NOMEM)
		ereport(ERROR,
				(errcode(ERRCODE_OUT_OF_MEMORY),
				 errmsg("out of memory in OpenCC while %s", what)));
	if (rc == OCPG_UNSUPPORTED)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("%s", ocpg_last_error())));
	ereport(ERROR,
			(errcode(ERRCODE_INTERNAL_ERROR),
			 errmsg("OpenCC failed while %s", what),
			 errdetail("%s", ocpg_last_error())));
}

static char *
config_list(void)
{
	StringInfoData buf;

	initStringInfo(&buf);
	for (int i = 0; i < ocpg_config_count; i++)
		appendStringInfo(&buf, "%s%s", i ? ", " : "", ocpg_configs[i].name);
	return buf.data;
}

static int
lookup_config(const char *name, size_t len)
{
	int			cfg = ocpg_config_lookup(name, len);

	if (cfg < 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("unknown OpenCC config \"%.*s\"", (int) len, name),
				 errdetail("Built-in configs: %s.", config_list()),
				 errhint("SELECT * FROM opencc.configs lists them.")));
	return cfg;
}

static int
config_arg(FunctionCallInfo fcinfo, int argno)
{
	text	   *t = PG_GETARG_TEXT_PP(argno);

	return lookup_config(VARDATA_ANY(t), VARSIZE_ANY_EXHDR(t));
}

static text *
convert_text(int cfg, const char *in, size_t len)
{
	const char *out;
	size_t		outlen;
	int			rc;
	text	   *result;

	if (len == 0)
		return cstring_to_text_with_len("", 0);
	rc = ocpg_convert(cfg, in, len, &out, &outlen);
	if (rc != OCPG_OK)
		engine_error(rc, "converting text");
	result = cstring_to_text_with_len(out, (int) outlen);
	ocpg_trim();
	return result;
}

static ocpg_script_counts
script_counts(text *t)
{
	ocpg_script_counts c;
	int			rc = ocpg_script_stats(VARDATA_ANY(t), VARSIZE_ANY_EXHDR(t), &c);

	if (rc != OCPG_OK)
		engine_error(rc, "classifying characters");
	return c;
}

static const char *
classify(const ocpg_script_counts *c)
{
	int32		marked = c->simplified + c->traditional;

	if (c->han + c->kana == 0)
		return "none";
	if ((int64) c->kana * 10 >= (int64) c->han + c->kana)
		return "japanese";
	if (marked == 0)
		return "neutral";
	if ((int64) c->simplified * 5 >= (int64) marked * 4)
		return "simplified";
	if ((int64) c->traditional * 5 >= (int64) marked * 4)
		return "traditional";
	return "mixed";
}

static bool
region_matches(const char *tag, size_t len, const char *name)
{
	if (strlen(name) != len)
		return false;
	for (size_t i = 0; i < len; i++)
	{
		char		a = tag[i] == '_' ? '-' : pg_ascii_tolower((unsigned char) tag[i]);
		char		b = pg_ascii_tolower((unsigned char) name[i]);

		if (a != b)
			return false;
	}
	return true;
}

static Region
parse_region(text *t, const char *role)
{
	const char *s = VARDATA_ANY(t);
	size_t		len = VARSIZE_ANY_EXHDR(t);

	for (int r = 0; r < R_COUNT; r++)
		if (region_matches(s, len, region_names[r]))
			return (Region) r;
	ereport(ERROR,
			(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			 errmsg("unsupported %s region \"%.*s\"", role, (int) len, s),
			 errhint("Supported regions: zh-Hans, zh-Hant, zh-CN, zh-TW, zh-HK, ja%s.",
					 strcmp(role, "source") == 0 ? ", or auto" : "")));
	return R_HANS;				/* keep compiler quiet */
}

static const Route *
find_route(Region source, Region target)
{
	for (size_t i = 0; i < lengthof(routes); i++)
		if (routes[i].source == source && routes[i].target == target)
			return &routes[i];
	elog(ERROR, "no route from %s to %s", region_names[source], region_names[target]);
	return NULL;				/* keep compiler quiet */
}

static const char *
route_maturity(const Route *route)
{
	static const char *const levels[] = {"stable", "developing", "experimental"};
	int			worst = 0;

	for (int i = 0; i < 2 && route->chain[i]; i++)
	{
		int			cfg = ocpg_config_lookup(route->chain[i], strlen(route->chain[i]));

		for (int l = 0; l < 3; l++)
			if (strcmp(ocpg_configs[cfg].maturity, levels[l]) == 0 && l > worst)
				worst = l;
	}
	return levels[worst];
}

/* Classic materialize-mode SRF setup; works on every supported release. */
static Tuplestorestate *
materialize(FunctionCallInfo fcinfo, TupleDesc *tupdesc)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
	MemoryContext oldcxt;
	Tuplestorestate *store;

	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that cannot accept a set")));
	if (!(rsinfo->allowedModes & SFRM_Materialize))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not allowed in this context")));

	oldcxt = MemoryContextSwitchTo(rsinfo->econtext->ecxt_per_query_memory);
	if (get_call_result_type(fcinfo, NULL, tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	store = tuplestore_begin_heap(true, false, work_mem);
	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = store;
	rsinfo->setDesc = *tupdesc;
	MemoryContextSwitchTo(oldcxt);
	return store;
}

static Datum
cell_datum(const ocpg_table *t, int row, int col)
{
	size_t		len;
	const char *s = ocpg_table_cell(t, row, col, &len);

	return PointerGetDatum(cstring_to_text_with_len(s, (int) len));
}

static Datum
cells_array(const ocpg_table *t, int row, int from)
{
	int			n = ocpg_table_cols(t, row) - from;
	Datum	   *elems;

	if (n <= 0)
		return PointerGetDatum(construct_empty_array(TEXTOID));
	elems = palloc(sizeof(Datum) * n);
	for (int i = 0; i < n; i++)
		elems[i] = cell_datum(t, row, from + i);
	return PointerGetDatum(construct_array(elems, n, TEXTOID, -1, false, TYPALIGN_INT));
}

/* ------------------------------------------------------------------------
 * convert(input text, config text) RETURNS text
 * ------------------------------------------------------------------------
 */
Datum
opencc_convert(PG_FUNCTION_ARGS)
{
	text	   *input = PG_GETARG_TEXT_PP(0);
	int			cfg = config_arg(fcinfo, 1);

	check_encoding();
	PG_RETURN_TEXT_P(convert_text(cfg, VARDATA_ANY(input), VARSIZE_ANY_EXHDR(input)));
}

/* ------------------------------------------------------------------------
 * localize(input text, target text, source text DEFAULT 'auto') RETURNS text
 * ------------------------------------------------------------------------
 */
Datum
opencc_localize(PG_FUNCTION_ARGS)
{
	text	   *input = PG_GETARG_TEXT_PP(0);
	Region		target = parse_region(PG_GETARG_TEXT_PP(1), "target");
	text	   *srcarg = PG_GETARG_TEXT_PP(2);
	Region		source;
	const Route *route;

	check_encoding();
	if (region_matches(VARDATA_ANY(srcarg), VARSIZE_ANY_EXHDR(srcarg), "auto"))
	{
		ocpg_script_counts c = script_counts(input);
		const char *kind = classify(&c);

		if (strcmp(kind, "japanese") == 0)
			source = R_JA;
		else if (target == R_HANS || target == R_CN)
			source = (strcmp(kind, "traditional") == 0 || strcmp(kind, "mixed") == 0) ? R_HANT : R_HANS;
		else
			source = strcmp(kind, "traditional") == 0 ? R_HANT : R_HANS;
	}
	else
		source = parse_region(srcarg, "source");

	route = find_route(source, target);
	for (int i = 0; i < 2 && route->chain[i]; i++)
	{
		int			cfg = ocpg_config_lookup(route->chain[i], strlen(route->chain[i]));

		input = convert_text(cfg, VARDATA_ANY(input), VARSIZE_ANY_EXHDR(input));
	}
	PG_RETURN_TEXT_P(input);
}

/* ------------------------------------------------------------------------
 * detect(input text) RETURNS text
 * script_stats(input text, OUT han, OUT simplified, OUT traditional, OUT kana)
 * ------------------------------------------------------------------------
 */
Datum
opencc_detect(PG_FUNCTION_ARGS)
{
	ocpg_script_counts c;

	check_encoding();
	c = script_counts(PG_GETARG_TEXT_PP(0));
	PG_RETURN_TEXT_P(cstring_to_text(classify(&c)));
}

Datum
opencc_script_stats(PG_FUNCTION_ARGS)
{
	ocpg_script_counts c;
	TupleDesc	tupdesc;
	Datum		values[4];
	bool		nulls[4] = {false, false, false, false};

	check_encoding();
	c = script_counts(PG_GETARG_TEXT_PP(0));
	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	values[0] = Int32GetDatum(c.han);
	values[1] = Int32GetDatum(c.simplified);
	values[2] = Int32GetDatum(c.traditional);
	values[3] = Int32GetDatum(c.kana);
	PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(BlessTupleDesc(tupdesc), values, nulls)));
}

/* ------------------------------------------------------------------------
 * candidates(word text, config text) RETURNS text[]
 * ------------------------------------------------------------------------
 */
Datum
opencc_candidates(PG_FUNCTION_ARGS)
{
	text	   *word = PG_GETARG_TEXT_PP(0);
	int			cfg = config_arg(fcinfo, 1);
	ocpg_table *t;
	int			rc;
	Datum		result;

	check_encoding();
	rc = ocpg_candidates(cfg, VARDATA_ANY(word), VARSIZE_ANY_EXHDR(word), &t);
	if (rc != OCPG_OK)
		engine_error(rc, "listing candidates");
	PG_TRY();
	{
		result = cells_array(t, 0, 0);
	}
	PG_FINALLY();
	{
		ocpg_table_free(t);
	}
	PG_END_TRY();
	PG_RETURN_DATUM(result);
}

/* ------------------------------------------------------------------------
 * explain(input text, config text)
 *     RETURNS TABLE (seq int, source text, steps text[], output text)
 * ------------------------------------------------------------------------
 */
Datum
opencc_explain(PG_FUNCTION_ARGS)
{
	text	   *input = PG_GETARG_TEXT_PP(0);
	int			cfg = config_arg(fcinfo, 1);
	TupleDesc	tupdesc;
	Tuplestorestate *store;
	ocpg_table *t;
	int			rc;

	check_encoding();
	store = materialize(fcinfo, &tupdesc);
	rc = ocpg_explain(cfg, VARDATA_ANY(input), VARSIZE_ANY_EXHDR(input), &t);
	if (rc != OCPG_OK)
		engine_error(rc, "inspecting a conversion");
	PG_TRY();
	{
		for (int row = 0; row < ocpg_table_rows(t); row++)
		{
			Datum		values[4];
			bool		nulls[4] = {false, false, false, false};
			int			cols = ocpg_table_cols(t, row);

			values[0] = Int32GetDatum(row + 1);
			values[1] = cell_datum(t, row, 0);
			values[2] = cells_array(t, row, 1);
			values[3] = cell_datum(t, row, cols - 1);
			tuplestore_putvalues(store, tupdesc, values, nulls);
		}
	}
	PG_FINALLY();
	{
		ocpg_table_free(t);
	}
	PG_END_TRY();
	return (Datum) 0;
}

/* ------------------------------------------------------------------------
 * ambiguities(input text, config text)
 *     RETURNS TABLE (pos int, len int, source text, output text, candidates text[])
 * ------------------------------------------------------------------------
 */
Datum
opencc_ambiguities(PG_FUNCTION_ARGS)
{
	text	   *input = PG_GETARG_TEXT_PP(0);
	int			cfg = config_arg(fcinfo, 1);
	TupleDesc	tupdesc;
	Tuplestorestate *store;
	ocpg_table *t;
	int			rc;

	check_encoding();
	store = materialize(fcinfo, &tupdesc);
	rc = ocpg_ambiguities(cfg, VARDATA_ANY(input), VARSIZE_ANY_EXHDR(input), &t);
	if (rc != OCPG_OK)
		engine_error(rc, "annotating ambiguities");
	PG_TRY();
	{
		for (int row = 0; row < ocpg_table_rows(t); row++)
		{
			Datum		values[5];
			bool		nulls[5] = {false, false, false, false, false};

			values[0] = Int32GetDatum(ocpg_table_num(t, row, 0));
			values[1] = Int32GetDatum(ocpg_table_num(t, row, 1));
			values[2] = cell_datum(t, row, 0);
			values[3] = cell_datum(t, row, 1);
			values[4] = cells_array(t, row, 2);
			tuplestore_putvalues(store, tupdesc, values, nulls);
		}
	}
	PG_FINALLY();
	{
		ocpg_table_free(t);
	}
	PG_END_TRY();
	return (Datum) 0;
}

/* ------------------------------------------------------------------------
 * version(OUT extension text, OUT opencc text, OUT dictionary text)
 * ------------------------------------------------------------------------
 */
Datum
opencc_version(PG_FUNCTION_ARGS)
{
	TupleDesc	tupdesc;
	Datum		values[3];
	bool		nulls[3] = {false, false, false};

	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	values[0] = CStringGetTextDatum(OPENCC_PG_VERSION);
	values[1] = CStringGetTextDatum(ocpg_opencc_version);
	values[2] = CStringGetTextDatum(ocpg_resource_digest);
	PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(BlessTupleDesc(tupdesc), values, nulls)));
}

/* ------------------------------------------------------------------------
 * _configs() and _routes(): back the opencc.configs and opencc.routes views
 * ------------------------------------------------------------------------
 */
Datum
opencc_configs(PG_FUNCTION_ARGS)
{
	TupleDesc	tupdesc;
	Tuplestorestate *store = materialize(fcinfo, &tupdesc);

	for (int i = 0; i < ocpg_config_count; i++)
	{
		const ocpg_config_info *c = &ocpg_configs[i];
		Datum		values[7];
		bool		nulls[7] = {false, false, false, false, false, false, false};

		values[0] = CStringGetTextDatum(c->name);
		values[1] = CStringGetTextDatum(c->source);
		values[2] = CStringGetTextDatum(c->target);
		values[3] = BoolGetDatum(c->phrases != 0);
		values[4] = CStringGetTextDatum(c->maturity);
		values[5] = CStringGetTextDatum(c->description);
		values[6] = BoolGetDatum(ocpg_config_loaded(i) != 0);
		tuplestore_putvalues(store, tupdesc, values, nulls);
	}
	return (Datum) 0;
}

Datum
opencc_routes(PG_FUNCTION_ARGS)
{
	TupleDesc	tupdesc;
	Tuplestorestate *store = materialize(fcinfo, &tupdesc);

	for (size_t i = 0; i < lengthof(routes); i++)
	{
		const Route *r = &routes[i];
		Datum		values[4];
		bool		nulls[4] = {false, false, false, false};
		Datum		chain[2];
		int			n = 0;

		for (; n < 2 && r->chain[n]; n++)
			chain[n] = CStringGetTextDatum(r->chain[n]);
		values[0] = CStringGetTextDatum(region_names[r->source]);
		values[1] = CStringGetTextDatum(region_names[r->target]);
		values[2] = n ? PointerGetDatum(construct_array(chain, n, TEXTOID, -1, false, TYPALIGN_INT))
			: PointerGetDatum(construct_empty_array(TEXTOID));
		values[3] = CStringGetTextDatum(route_maturity(r));
		tuplestore_putvalues(store, tupdesc, values, nulls);
	}
	return (Datum) 0;
}

/* ------------------------------------------------------------------------
 * opencc.preload: configs to load when the library is loaded
 * ------------------------------------------------------------------------
 */
static bool
for_each_preload(const char *value, bool load, char **bad)
{
	char	   *copy = pstrdup(value);
	List	   *names;
	ListCell   *lc;
	bool		ok = true;

	if (!SplitIdentifierString(copy, ',', &names))
	{
		*bad = pstrdup(value);
		return false;
	}
	foreach(lc, names)
	{
		const char *name = (const char *) lfirst(lc);

		if (strcmp(name, "all") == 0)
		{
			for (int i = 0; load && i < ocpg_config_count; i++)
				if (ocpg_load(i) != OCPG_OK)
					ereport(WARNING,
							(errmsg("opencc could not preload config \"%s\"", ocpg_configs[i].name),
							 errdetail("%s", ocpg_last_error())));
			continue;
		}
		{
			int			cfg = ocpg_config_lookup(name, strlen(name));

			if (cfg < 0)
			{
				*bad = pstrdup(name);
				ok = false;
				break;
			}
			if (load && ocpg_load(cfg) != OCPG_OK)
				ereport(WARNING,
						(errmsg("opencc could not preload config \"%s\"", name),
						 errdetail("%s", ocpg_last_error())));
		}
	}
	list_free(names);
	pfree(copy);
	return ok;
}

static bool
check_preload(char **newval, void **extra, GucSource source)
{
	char	   *bad = NULL;

	if (*newval == NULL || **newval == '\0')
		return true;
	if (!for_each_preload(*newval, false, &bad))
	{
		GUC_check_errdetail("\"%s\" is not a built-in OpenCC config; use names from opencc.configs or \"all\".",
							bad ? bad : *newval);
		return false;
	}
	return true;
}

void
_PG_init(void)
{
	DefineCustomStringVariable("opencc.preload",
							   "OpenCC configs to load when the library is loaded.",
							   "Comma-separated config names, or \"all\". Add opencc to "
							   "shared_preload_libraries so that every backend inherits them.",
							   &opencc_preload,
							   "",
							   PGC_SIGHUP,
							   GUC_LIST_INPUT,
							   check_preload, NULL, NULL);
#if PG_VERSION_NUM >= 150000
	MarkGUCPrefixReserved("opencc");
#else
	EmitWarningsOnPlaceholders("opencc");
#endif

	if (opencc_preload != NULL && opencc_preload[0] != '\0')
	{
		char	   *bad = NULL;

		(void) for_each_preload(opencc_preload, true, &bad);
	}
}

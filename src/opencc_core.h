/*
 * opencc_core.h
 *    C interface to the embedded OpenCC engine.
 *
 * Everything behind this header is C++.  No function here throws, calls
 * PostgreSQL, or allocates with palloc: errors come back as return codes with
 * a message in ocpg_last_error(), and results live in engine-owned memory that
 * the caller copies out.  That keeps C++ exceptions and PostgreSQL's longjmp
 * error handling from ever crossing each other.
 */
#ifndef OPENCC_CORE_H
#define OPENCC_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OCPG_OK				0
#define OCPG_ERROR			(-1)	/* OpenCC raised an error */
#define OCPG_NOMEM			(-2)	/* allocation failed */
#define OCPG_UNSUPPORTED	(-3)	/* operation not available for config */

typedef struct ocpg_config_info
{
	const char *name;			/* OpenCC config name, e.g. "s2twp" */
	const char *source;			/* language tag of the input */
	const char *target;			/* language tag of the output */
	int			phrases;		/* converts regional vocabulary, not just glyphs */
	const char *maturity;		/* stable / developing / experimental */
	const char *description;
} ocpg_config_info;

extern const ocpg_config_info ocpg_configs[];
extern const int ocpg_config_count;

/* Index of a built-in config by name, or -1. */
int			ocpg_config_lookup(const char *name, size_t len);
int			ocpg_config_loaded(int cfg);
int			ocpg_load(int cfg);

/*
 * Convert len bytes of UTF-8.  On success *out points to engine-owned memory
 * that stays valid until the next engine call.
 */
int			ocpg_convert(int cfg, const char *in, size_t len,
						 const char **out, size_t *outlen);
/* Drop a large result buffer kept from the last conversion. */
void		ocpg_trim(void);

/* A small table of strings (plus two integers per row) owned by the engine. */
typedef struct ocpg_table ocpg_table;

/* One row; every column is a candidate form of the word. */
int			ocpg_candidates(int cfg, const char *word, size_t len, ocpg_table **out);
/* One row per segment: [source, stage1, stage2, ...]. */
int			ocpg_explain(int cfg, const char *in, size_t len, ocpg_table **out);
/* One row per one-to-many span: [source, output, candidates...]; nums = pos, len. */
int			ocpg_ambiguities(int cfg, const char *in, size_t len, ocpg_table **out);

int			ocpg_table_rows(const ocpg_table *t);
int			ocpg_table_cols(const ocpg_table *t, int row);
const char *ocpg_table_cell(const ocpg_table *t, int row, int col, size_t *len);
int32_t		ocpg_table_num(const ocpg_table *t, int row, int k);
void		ocpg_table_free(ocpg_table *t);

typedef struct ocpg_script_counts
{
	int32_t		han;			/* CJK ideographs */
	int32_t		simplified;		/* characters only used in Simplified Chinese */
	int32_t		traditional;	/* characters only used in Traditional Chinese */
	int32_t		kana;			/* Hiragana and Katakana */
} ocpg_script_counts;

int			ocpg_script_stats(const char *in, size_t len, ocpg_script_counts *out);

const char *ocpg_last_error(void);

#ifdef __cplusplus
}
#endif

#endif							/* OPENCC_CORE_H */

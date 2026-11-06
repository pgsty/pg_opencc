/*
 * opencc_core.cpp
 *    Embedded OpenCC engine behind the C interface in opencc_core.h.
 *
 * Converters are created lazily, once per process, from the configs and
 * dictionaries compiled into the shared library, and are kept for the life of
 * the process.  OpenCC's own dictionary cache shares a dictionary between the
 * converters that use it.
 */
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Config.hpp"
#include "ConversionAmbiguities.hpp"
#include "ConversionCandidates.hpp"
#include "ConversionInspection.hpp"
#include "Converter.hpp"
#include "DictEntry.hpp"
#include "Exception.hpp"
#include "Lexicon.hpp"
#include "MarisaDict.hpp"
#include "ResourceProvider.hpp"

#include "opencc_core.h"
#include "opencc_resources.h"

extern "C" {

const ocpg_config_info ocpg_configs[] = {
	{"s2t", "zh-Hans", "zh-Hant", 0, "stable", "Simplified Chinese to Traditional Chinese (OpenCC standard)"},
	{"t2s", "zh-Hant", "zh-Hans", 0, "stable", "Traditional Chinese (OpenCC standard) to Simplified Chinese"},
	{"s2tw", "zh-Hans", "zh-TW", 0, "stable", "Simplified Chinese to Traditional Chinese (Taiwan standard)"},
	{"tw2s", "zh-TW", "zh-Hans", 0, "stable", "Traditional Chinese (Taiwan standard) to Simplified Chinese"},
	{"s2hk", "zh-Hans", "zh-HK", 0, "stable", "Simplified Chinese to Traditional Chinese (Hong Kong variant)"},
	{"hk2s", "zh-HK", "zh-Hans", 0, "stable", "Traditional Chinese (Hong Kong variant) to Simplified Chinese"},
	{"s2twp", "zh-Hans", "zh-TW", 1, "stable", "Simplified Chinese to Traditional Chinese (Taiwan standard) with Taiwan phrases"},
	{"tw2sp", "zh-TW", "zh-Hans", 1, "stable", "Traditional Chinese (Taiwan standard) to Simplified Chinese with Mainland phrases"},
	{"s2hkp", "zh-Hans", "zh-HK", 1, "developing", "Simplified Chinese to Traditional Chinese (Hong Kong variant) with Hong Kong phrases"},
	{"hk2sp", "zh-HK", "zh-Hans", 1, "developing", "Traditional Chinese (Hong Kong variant) to Simplified Chinese with Mainland phrases"},
	{"t2tw", "zh-Hant", "zh-TW", 0, "stable", "Traditional Chinese (OpenCC standard) to Taiwan standard"},
	{"tw2t", "zh-TW", "zh-Hant", 0, "stable", "Traditional Chinese (Taiwan standard) to OpenCC standard"},
	{"t2hk", "zh-Hant", "zh-HK", 0, "stable", "Traditional Chinese (OpenCC standard) to Hong Kong variant"},
	{"hk2t", "zh-HK", "zh-Hant", 0, "stable", "Traditional Chinese (Hong Kong variant) to OpenCC standard"},
	{"t2jp", "zh-Hant", "ja", 0, "experimental", "Japanese Kyujitai (old Kanji) to Shinjitai (new Kanji)"},
	{"jp2t", "ja", "zh-Hant", 0, "experimental", "Japanese Shinjitai (new Kanji) to Kyujitai (old Kanji)"},
};

const int ocpg_config_count = sizeof(ocpg_configs) / sizeof(ocpg_configs[0]);

}								/* extern "C" */

struct ocpg_table
{
	std::vector<std::vector<std::string>> rows;
	std::vector<std::pair<int32_t, int32_t>> nums;
};

namespace
{

char last_error[1024];

void
set_error(const char *msg)
{
	std::snprintf(last_error, sizeof(last_error), "%s", msg ? msg : "unknown error");
}

const ocpg_resource *
find_resource(std::string_view name)
{
	for (size_t i = 0; i < ocpg_resource_count; i++)
		if (name == ocpg_resources[i].name)
			return &ocpg_resources[i];
	return nullptr;
}

/* Serves OpenCC configs and dictionaries from the embedded table only. */
class EmbeddedResourceProvider : public opencc::ResourceProvider
{
public:
	std::string Resolve(std::string_view name) const override
	{
		/* never fall back to the filesystem */
		throw opencc::FileNotFound("embedded:" + std::string(name));
	}

	std::shared_ptr<const Resource> GetResource(std::string_view name) const override
	{
		const ocpg_resource *r = find_resource(name);

		if (r == nullptr)
			throw opencc::FileNotFound("embedded:" + std::string(name));
		return std::make_shared<const Resource>(std::string(name),
												reinterpret_cast<const char *>(r->data),
												r->size, nullptr,
												"embedded\n" + std::string(name));
	}
};

/* 1 = only used in Simplified Chinese, 2 = only used in Traditional Chinese */
using ScriptTable = std::unordered_map<uint32_t, uint8_t>;

struct Engine
{
	opencc::Config config;
	std::shared_ptr<opencc::ResourceProvider> provider =
		std::make_shared<EmbeddedResourceProvider>();
	std::vector<opencc::ConverterPtr> converters =
		std::vector<opencc::ConverterPtr>(ocpg_config_count);
	std::unique_ptr<ScriptTable> scripts;
	std::string buffer;
};

/* Created on first use and intentionally never destroyed. */
Engine &
engine()
{
	static Engine *e = new Engine();

	return *e;
}

const opencc::Converter &
converter(int cfg)
{
	Engine	   &e = engine();

	if (cfg < 0 || cfg >= ocpg_config_count)
		throw opencc::Exception("invalid OpenCC config index");
	if (!e.converters[cfg])
	{
		std::string name = std::string(ocpg_configs[cfg].name) + ".json";
		const ocpg_resource *r = find_resource(name);

		if (r == nullptr)
			throw opencc::FileNotFound("embedded:" + name);
		e.converters[cfg] = e.config.NewFromString(
			std::string(reinterpret_cast<const char *>(r->data), r->size), e.provider);
	}
	return *e.converters[cfg];
}

/* Decode one code point from valid UTF-8 (PostgreSQL validates text input). */
uint32_t
next_cp(const unsigned char *&p, const unsigned char *end)
{
	uint32_t	c = *p++;
	int			extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;

	if (extra)
		c &= (0x3F >> extra);
	while (extra-- > 0 && p < end)
		c = (c << 6) | (*p++ & 0x3F);
	return c;
}

int32_t
count_chars(const char *s, size_t len)
{
	int32_t		n = 0;

	for (size_t i = 0; i < len; i++)
		if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
			n++;
	return n;
}

bool
single_cp(const std::string &s, uint32_t *cp)
{
	if (s.empty())
		return false;
	const unsigned char *p = reinterpret_cast<const unsigned char *>(s.data());
	const unsigned char *end = p + s.size();

	*cp = next_cp(p, end);
	return p == end;
}

/*
 * An embedded dictionary with its lexicon.  Both are held explicitly: in
 * C++17 "for (x : *make_shared_thing())" frees the object before the loop.
 */
struct LoadedLexicon
{
	opencc::MarisaDictPtr dict;
	opencc::LexiconPtr lexicon;
};

LoadedLexicon
load_lexicon(const char *name)
{
	const ocpg_resource *r = find_resource(name);
	LoadedLexicon l;

	if (r == nullptr)
		throw opencc::FileNotFound(std::string("embedded:") + name);
	l.dict = opencc::MarisaDict::NewFromBuffer(reinterpret_cast<const char *>(r->data), r->size);
	l.lexicon = l.dict->GetLexicon();
	return l;
}

/* Characters a dictionary maps away from themselves (key not among values). */
std::vector<uint32_t>
non_identity_keys(const char *name)
{
	std::vector<uint32_t> out;
	LoadedLexicon l = load_lexicon(name);

	for (const auto &entry : *l.lexicon)
	{
		uint32_t	cp;
		std::string key = entry->Key();

		if (!single_cp(key, &cp))
			continue;
		std::vector<std::string> values = entry->Values();
		if (std::find(values.begin(), values.end(), key) == values.end())
			out.push_back(cp);
	}
	return out;
}

/*
 * A character is "simplified" when STCharacters converts it and it is not a
 * Taiwan or Hong Kong standard form; "traditional" when TSCharacters converts
 * it.  Characters claimed by both sides are left unmarked.
 */
const ScriptTable &
scripts()
{
	Engine	   &e = engine();

	if (!e.scripts)
	{
		std::unique_ptr<ScriptTable> table(new ScriptTable());
		std::unordered_map<uint32_t, uint8_t> side;
		std::vector<uint32_t> regional;

		for (uint32_t cp : non_identity_keys("STCharacters.ocd2"))
			side[cp] |= 1;
		for (uint32_t cp : non_identity_keys("TSCharacters.ocd2"))
			side[cp] |= 2;
		for (const char *name : {"TWVariants.ocd2", "HKVariants.ocd2"})
		{
			LoadedLexicon l = load_lexicon(name);

			for (const auto &entry : *l.lexicon)
				for (const std::string &value : entry->Values())
				{
					uint32_t	cp;

					if (single_cp(value, &cp))
						regional.push_back(cp);
				}
		}
		for (uint32_t cp : regional)
		{
			auto		it = side.find(cp);

			if (it != side.end() && it->second == 1)
				side.erase(it);
		}
		for (const auto &kv : side)
			if (kv.second == 1 || kv.second == 2)
				(*table)[kv.first] = kv.second;
		e.scripts = std::move(table);
	}
	return *e.scripts;
}

bool
is_han(uint32_t c)
{
	return (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) ||
		(c >= 0xF900 && c <= 0xFAFF) || (c >= 0x20000 && c <= 0x323AF);
}

bool
is_kana(uint32_t c)
{
	return c >= 0x3040 && c <= 0x30FF;
}

}								/* namespace */

#define OCPG_GUARD(body) \
	try { body } \
	catch (const std::bad_alloc &) { set_error("out of memory in OpenCC"); return OCPG_NOMEM; } \
	catch (const opencc::Exception &ex) { set_error(ex.what()); return OCPG_ERROR; } \
	catch (const std::exception &ex) { set_error(ex.what()); return OCPG_ERROR; } \
	catch (...) { set_error("unknown OpenCC error"); return OCPG_ERROR; }

extern "C" {

int
ocpg_config_lookup(const char *name, size_t len)
{
	for (int i = 0; i < ocpg_config_count; i++)
		if (std::strlen(ocpg_configs[i].name) == len &&
			std::memcmp(ocpg_configs[i].name, name, len) == 0)
			return i;
	return -1;
}

int
ocpg_config_loaded(int cfg)
{
	try
	{
		return cfg >= 0 && cfg < ocpg_config_count && engine().converters[cfg] != nullptr;
	}
	catch (...)
	{
		return 0;
	}
}

int
ocpg_load(int cfg)
{
	OCPG_GUARD(converter(cfg); return OCPG_OK;)
}

int
ocpg_convert(int cfg, const char *in, size_t len, const char **out, size_t *outlen)
{
	OCPG_GUARD(
		const opencc::Converter &c = converter(cfg);
		Engine &e = engine();

		e.buffer = c.Convert(std::string_view(in, len));
		*out = e.buffer.data();
		*outlen = e.buffer.size();
		return OCPG_OK;
	)
}

void
ocpg_trim(void)
{
	try
	{
		Engine	   &e = engine();

		if (e.buffer.capacity() > (size_t) 1024 * 1024)
			std::string().swap(e.buffer);
	}
	catch (...)
	{
	}
}

int
ocpg_candidates(int cfg, const char *word, size_t len, ocpg_table **out)
{
	*out = nullptr;
	OCPG_GUARD(
		std::unique_ptr<ocpg_table> t(new ocpg_table());

		t->rows.push_back(opencc::GetAllConversions(converter(cfg), std::string_view(word, len)));
		t->nums.push_back({0, 0});
		*out = t.release();
		return OCPG_OK;
	)
}

int
ocpg_explain(int cfg, const char *in, size_t len, ocpg_table **out)
{
	*out = nullptr;
	OCPG_GUARD(
		std::unique_ptr<ocpg_table> t(new ocpg_table());
		opencc::ConversionInspectionResult r = converter(cfg).Inspect(std::string_view(in, len));
		const opencc::ConversionInspectionResult *main = &r;

		/* configs with a normalization pre-pass are pipelines: [normalize, convert] */
		while (main->segments.empty() && !main->pipelineStages.empty())
			main = &main->pipelineStages.back();

		bool aligned = !main->segments.empty();
		for (const auto &stage : main->stages)
			if (stage.segments.size() != main->segments.size())
				aligned = false;

		if (aligned)
		{
			for (size_t i = 0; i < main->segments.size(); i++)
			{
				std::vector<std::string> row;

				row.push_back(main->segments[i]);
				for (const auto &stage : main->stages)
					row.push_back(stage.segments[i]);
				if (main->stages.empty())
					row.push_back(main->segments[i]);
				t->rows.push_back(std::move(row));
				t->nums.push_back({0, 0});
			}
		}
		else if (!r.input.empty())
		{
			t->rows.push_back({main->input.empty() ? r.input : main->input, r.output});
			t->nums.push_back({0, 0});
		}
		*out = t.release();
		return OCPG_OK;
	)
}

int
ocpg_ambiguities(int cfg, const char *in, size_t len, ocpg_table **out)
{
	*out = nullptr;
	OCPG_GUARD(
		const opencc::Converter &c = converter(cfg);
		opencc::AnnotatedConversion a = opencc::ConvertWithAmbiguities(c, std::string_view(in, len));
		std::unique_ptr<ocpg_table> t(new ocpg_table());
		std::vector<std::vector<std::string>> cache(a.sources.size());
		std::vector<bool> cached(a.sources.size(), false);
		size_t byte = 0;
		int32_t chars = 0;

		if (!a.analyzed)
		{
			set_error("OpenCC cannot analyze ambiguities for this config");
			return OCPG_UNSUPPORTED;
		}
		for (const auto &span : a.ambiguities)
		{
			std::vector<std::string> row;

			chars += count_chars(a.output.data() + byte, span.outputOffset - byte);
			byte = span.outputOffset;
			if (!cached[span.sourceIndex])
			{
				cache[span.sourceIndex] = opencc::GetAllConversions(c, a.sources[span.sourceIndex]);
				cached[span.sourceIndex] = true;
			}
			row.push_back(a.sources[span.sourceIndex]);
			row.push_back(a.output.substr(span.outputOffset, span.outputLength));
			row.insert(row.end(), cache[span.sourceIndex].begin(), cache[span.sourceIndex].end());
			t->rows.push_back(std::move(row));
			t->nums.push_back({chars + 1,
							   count_chars(a.output.data() + span.outputOffset, span.outputLength)});
		}
		*out = t.release();
		return OCPG_OK;
	)
}

int
ocpg_table_rows(const ocpg_table *t)
{
	return t ? static_cast<int>(t->rows.size()) : 0;
}

int
ocpg_table_cols(const ocpg_table *t, int row)
{
	return static_cast<int>(t->rows[row].size());
}

const char *
ocpg_table_cell(const ocpg_table *t, int row, int col, size_t *len)
{
	const std::string &s = t->rows[row][col];

	*len = s.size();
	return s.data();
}

int32_t
ocpg_table_num(const ocpg_table *t, int row, int k)
{
	return k == 0 ? t->nums[row].first : t->nums[row].second;
}

void
ocpg_table_free(ocpg_table *t)
{
	delete t;
}

int
ocpg_script_stats(const char *in, size_t len, ocpg_script_counts *out)
{
	std::memset(out, 0, sizeof(*out));
	OCPG_GUARD(
		const ScriptTable &table = scripts();
		const unsigned char *p = reinterpret_cast<const unsigned char *>(in);
		const unsigned char *end = p + len;

		while (p < end)
		{
			uint32_t	cp = next_cp(p, end);

			if (cp < 0x3040)
				continue;
			if (is_kana(cp))
				out->kana++;
			else if (is_han(cp))
			{
				out->han++;
				auto		it = table.find(cp);

				if (it != table.end())
					(it->second == 1 ? out->simplified : out->traditional)++;
			}
		}
		return OCPG_OK;
	)
}

const char *
ocpg_last_error(void)
{
	return last_error;
}

}								/* extern "C" */

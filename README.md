# pg_opencc

`opencc` is a PostgreSQL extension for converting among Simplified Chinese,
Traditional Chinese, Taiwan and Hong Kong conventions, and Japanese Shinjitai.
It is powered by [OpenCC](https://github.com/BYVoid/OpenCC), the conversion
engine behind most Chinese input methods and e-book tools.

```sql
CREATE EXTENSION opencc;

SELECT opencc.s2twp('鼠标里面的硅二极管坏了，导致光标分辨率降低。');
--  滑鼠裡面的矽二極體壞了，導致游標解析度降低。

SELECT opencc.localize('克里斯托弗·诺兰的电影，安装内存条', 'zh-HK');
--  基斯杜化·路蘭的電影，安裝記憶體模組
```

- **Self-contained.** OpenCC 1.4.2 and all of its dictionaries are compiled
  into `opencc.so`. There is no `libopencc` runtime dependency, and no data
  files are read at run time.
- **Same answer everywhere.** A given extension version returns byte-identical
  results on every OS and architecture. That makes the functions safe to use in
  expression indexes, stored generated columns and CHECK constraints.
- **Phrase-aware.** `海内存知己` stays a poem (`海內存知己`), while `安装内存条`
  becomes Taiwan hardware vocabulary (`安裝記憶體模組`).
- **Explainable.** You can ask which spans are ambiguous, what the candidates
  are, and how each conversion stage rewrote each segment.
- **Safe and fast.** Each backend loads a converter once, on first use (or
  preloads it at server start). C++ exceptions never cross into PostgreSQL.

| Package | Extension | Schema | PostgreSQL | OpenCC | License |
|---|---|---|---|---|---|
| `pg_opencc` | `opencc` | `opencc` (fixed) | 14 – 18 | 1.4.2 (vendored) | Apache-2.0 |


## Install

### Build from source

You need the PostgreSQL server headers (`pg_config`), `make`, `cmake` ≥ 3.12,
a C++17 compiler and `python3`. Python is used only at build time, to generate
OpenCC's derived dictionaries.

```bash
# EL 8/9/10 with PGDG packages
sudo dnf install -y postgresql18-devel cmake gcc-c++ make python3
# Debian / Ubuntu with PGDG packages
sudo apt install -y postgresql-server-dev-18 cmake g++ make python3

make                      # builds vendored OpenCC, its dictionaries, then opencc.so
sudo make install
make installcheck         # 5 regression suites, including OpenCC's 556 golden cases
```

Pass `PG_CONFIG=/usr/pgsql-18/bin/pg_config` to pick a PostgreSQL version. If
`pg_config` reports LLVM JIT support but `clang` is not installed, build with
`make with_llvm=no`. Builds use about 20 MB in `build/`; `make clean` removes it.

```sql
CREATE EXTENSION opencc;   -- trusted: any user with CREATE on the database can run it
```

The database must use the `UTF8` encoding.


## Functions

### Convert with an OpenCC config

| Function | Returns |
|---|---|
| `opencc.convert(input text, config text)` | `text` |
| `opencc.s2t(text)`, `opencc.t2s(text)`, … one per config below | `text` |
| `opencc.configs` (view) | name, source, target, phrases, maturity, description, loaded |

| Config | From → To | Regional phrases | Maturity |
|---|---|---|---|
| `s2t` / `t2s` | Simplified ↔ Traditional (OpenCC standard) | | stable |
| `s2tw` / `tw2s` | Simplified ↔ Taiwan standard | | stable |
| `s2twp` / `tw2sp` | Simplified ↔ Taiwan standard | ✓ | stable |
| `s2hk` / `hk2s` | Simplified ↔ Hong Kong variant | | stable |
| `s2hkp` / `hk2sp` | Simplified ↔ Hong Kong variant | ✓ | developing |
| `t2tw` / `tw2t` | OpenCC standard ↔ Taiwan standard | | stable |
| `t2hk` / `hk2t` | OpenCC standard ↔ Hong Kong variant | | stable |
| `t2jp` / `jp2t` | Japanese Kyūjitai ↔ Shinjitai | | experimental |

The wrappers are inlinable SQL functions, so an index on `opencc.tw2sp(title)`
also serves `WHERE opencc.convert(title, 'tw2sp') = …`. Every function is
`IMMUTABLE STRICT PARALLEL SAFE`. An unknown config name raises an error that
lists the valid ones.

### Convert for a region

| Function | Returns |
|---|---|
| `opencc.localize(input text, target text, source text DEFAULT 'auto')` | `text` |
| `opencc.routes` (view) | source, target, chain, maturity |

Regions are `zh-Hans`, `zh-Hant`, `zh-CN`, `zh-TW`, `zh-HK` and `ja`. Tags are
case-insensitive, and `_` is accepted in place of `-`. The script tags
(`zh-Hans`, `zh-Hant`) change only character forms. The region tags (`zh-CN`,
`zh-TW`, `zh-HK`) also convert vocabulary. Pairs that OpenCC has no single
config for, such as Taiwan → Hong Kong, are chained through the OpenCC
standard; `opencc.routes` shows the chain for each pair.

With `source => 'auto'`, `detect()` picks between simplified, traditional and
Japanese input. It cannot tell Taiwan text from Hong Kong text, so pass the
source region whenever vocabulary should change:

```sql
SELECT opencc.localize('滑鼠裡面的矽二極體壞了', 'zh-CN', source => 'zh-TW');  -- 鼠标里面的硅二极管坏了
SELECT opencc.localize('滑鼠裡面的矽二極體壞了', 'zh-CN');                    -- 滑鼠里面的矽二极体坏了
```

### Detect the script

| Function | Returns |
|---|---|
| `opencc.detect(input text)` | `none`, `neutral`, `simplified`, `traditional`, `mixed` or `japanese` |
| `opencc.script_stats(input text)` | han, simplified, traditional, kana |

A character counts as simplified or traditional only when it is used in one
script alone, according to OpenCC's character tables. `中文` is therefore
`neutral`. The result is a heuristic and works best on sentences rather than
single words.

```sql
ALTER TABLE article ADD CHECK (opencc.detect(body) IN ('simplified', 'neutral', 'none'));
```

### Inspect a conversion

| Function | Returns |
|---|---|
| `opencc.ambiguities(input text, config text)` | pos, len, source, output, candidates |
| `opencc.candidates(word text, config text)` | `text[]`, default first; empty if no dictionary has the word |
| `opencc.explain(input text, config text)` | seq, source, steps, output |

```sql
SELECT * FROM opencc.ambiguities('大战文丑的时候，他的头发很干燥', 's2t');
--  pos | len | source | output | candidates
--    3 |   2 | 文丑   | 文丑   | {文丑,文醜}

SELECT seq, source, steps, output
  FROM opencc.explain('猜到整个系统是数据库连接池出了问题', 's2twp') WHERE source <> output;
--  数据库 | {數據庫,資料庫} | 資料庫     ← stage 1 changes characters, stage 2 Taiwan vocabulary
```

`ambiguities()` is a good way to build a proofreading queue: run it over a
table and send the flagged rows to an editor.

### Operations

| Function / setting | Purpose |
|---|---|
| `opencc.version()` | extension version, OpenCC version, SHA-256 of the embedded dictionaries |
| `opencc.dependents()` | indexes, stored generated columns, CHECK constraints and materialized views that use opencc, with the statement that rebuilds each |
| `opencc.preload` | configs to load at library load time: names separated by commas, or `all` |


## Behavior to know

**Immutability is per extension version.** The dictionaries change only when
the extension package changes. After upgrading to a release that ships a newer
OpenCC, compare `opencc.version()` and run the statements from
`opencc.dependents()`:

```sql
SELECT action FROM opencc.dependents();
--  REINDEX INDEX CONCURRENTLY public.doc_fold_idx
--  UPDATE public.article SET body_tw = DEFAULT
```

`dependents()` follows `pg_depend`. It does not see opencc calls hidden inside
your own functions.

**Generated columns must be `STORED`.** PostgreSQL 18 creates virtual generated
columns by default, and they cannot call user-defined functions.

**Memory.** Converters are built on first use and kept for the life of the
backend. On macOS arm64 the measured resident growth was about 6 MB for `t2s`,
28 MB for `s2t` and 42 MB for `s2twp`; the large Simplified→Traditional phrase
table accounts for most of it. With many connections, load the library at
server start so that all backends share the pages copy-on-write:

```
shared_preload_libraries = 'opencc'
opencc.preload = 's2t,t2s,s2twp,tw2sp'
```

**Throughput.** On the same machine, `s2t` converted 10.8 MiB in 0.11 s and
`s2twp` in 0.22 s. 200,000 short rows took 0.14 s with `s2t` and 0.32 s with
`s2twp`. The first call in a backend took 2–21 ms, depending on the config.

**Schema.** Objects live in the fixed schema `opencc`. `DROP EXTENSION opencc`
leaves the empty schema behind.


## How the build works

`vendor/opencc` is a trimmed copy of the OpenCC 1.4.2 release; see
[vendor/README.md](vendor/README.md). `make` runs these steps:

1. Builds OpenCC with CMake as static, PIC, hidden-visibility libraries
   (`libopencc.a`, `libmarisa.a`), and compiles its dictionaries to `.ocd2` on
   the build host.
2. Runs `tools/embed_resources.py`, which turns the 16 configs and the 22
   dictionaries they use (about 1.2 MB) into a C file, and records their
   SHA-256.
3. Links `opencc.so`. An embedded resource provider serves OpenCC from memory;
   on Linux a version script exports only the symbols PostgreSQL needs.

The extension never touches a system OpenCC, so a distribution `libopencc` (or
none at all) on the same host makes no difference.


## License

Apache License 2.0. The bundled OpenCC is Apache-2.0; marisa-trie and
darts-clone are BSD-2-Clause; RapidJSON is MIT. See [NOTICE](NOTICE).

# Vendored OpenCC

| | |
|---|---|
| Upstream | https://github.com/BYVoid/OpenCC |
| Release | `ver.1.4.2` (2026-08-22), commit `025f371dc76b598d77384fbdab90c937471844d8` |
| Tarball | https://github.com/BYVoid/OpenCC/archive/refs/tags/ver.1.4.2.tar.gz |
| SHA-256 | `8e5f5cf7fe195bd9b9be851adc9738c1ef7dc5c24441dd5878a56db4087a9a70` |
| Embedded data digest | printed by `make` and by `SELECT dictionary FROM opencc.version()` |

`opencc/` is that tarball trimmed by `tools/vendor-opencc.sh`. Nothing in it
is modified. Removed: the Jieba plugin, Python and Node bindings, Bazel and CI files,
GoogleTest, Google Benchmark, pybind11, docs, and marisa's bindings and tools.
Kept: the C++ library and CLI sources (the CLIs build the dictionaries),
dictionary sources and build scripts, the bundled marisa-trie, darts-clone,
RapidJSON and TCLAP, and `test/testcases/testcases.json`.

The CMake build is configured with `CMAKE_DISABLE_FIND_PACKAGE_Git=TRUE`.
Without it, OpenCC would read its version from this repository's git tags. It
then falls back to the version recorded in `cmake/GitVersion.cmake`.

## Updating

```bash
tools/vendor-opencc.sh <version> <tarball-sha256>
python3 tools/gen_upstream_test.py      # regenerate test/sql/opencc_upstream.sql
make clean && make && sudo make install && make installcheck
```

Then update this file, the version in `NOTICE`, the vendored version in
`README.md`, and `CHANGELOG.md`. A new OpenCC release changes conversion
results, so ship it as a new extension version and tell users to run
`opencc.dependents()`.

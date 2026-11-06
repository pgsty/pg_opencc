# Changelog

## 0.1.0 (unreleased)

First release.

- Vendors OpenCC 1.4.2 and links it statically; embeds the 16 built-in configs
  and their 22 dictionaries in the shared library.
- `opencc.convert()` and one wrapper per config (`s2t` … `jp2t`), plus the
  `opencc.configs` view.
- `opencc.localize()` for zh-Hans, zh-Hant, zh-CN, zh-TW, zh-HK and ja, plus the
  `opencc.routes` view.
- `opencc.detect()` and `opencc.script_stats()`.
- `opencc.explain()`, `opencc.ambiguities()` and `opencc.candidates()`.
- `opencc.version()`, `opencc.dependents()` and the `opencc.preload` setting.
- Regression tests, including all 556 golden cases from OpenCC 1.4.2.

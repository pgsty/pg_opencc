#!/usr/bin/env bash
#
# Re-create vendor/opencc from an upstream OpenCC release tarball.
#
#   tools/vendor-opencc.sh <version> <sha256> [output-dir]
#   tools/vendor-opencc.sh 1.4.2 ad2b5a1e...    # replaces vendor/opencc
#
# Only what the static library and its dictionaries need is kept: the Jieba
# plugin, Python/Node bindings, Bazel files, GoogleTest, benchmarks and docs
# are dropped.  OpenCC's golden test cases are kept for test/sql/opencc_upstream.sql.
# Afterwards: update vendor/README.md, run tools/gen_upstream_test.py,
# rebuild and run make installcheck.
#
set -euo pipefail

VERSION=${1:?usage: $0 <version> <sha256> [output-dir]}
SHA256=${2:?usage: $0 <version> <sha256> [output-dir]}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${3:-$ROOT/vendor/opencc}
URL="https://github.com/BYVoid/OpenCC/archive/refs/tags/ver.${VERSION}.tar.gz"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "downloading $URL"
curl -fsSL -o "$WORK/opencc.tar.gz" "$URL"
echo "$SHA256  $WORK/opencc.tar.gz" | shasum -a 256 -c -

mkdir "$WORK/src"
tar -xzf "$WORK/opencc.tar.gz" -C "$WORK/src" --strip-components=1
S="$WORK/src"

rm -rf "$OUT.new"
mkdir -p "$OUT.new/deps" "$OUT.new/test/testcases"
for f in CMakeLists.txt LICENSE AUTHORS NEWS.md README.md opencc.pc.in OpenCCConfig.cmake.in; do
	cp "$S/$f" "$OUT.new/"
done
cp -R "$S/cmake" "$S/src" "$S/data" "$OUT.new/"
for d in marisa-0.3.1 rapidjson-1.1.0 tclap-1.2.5 darts-clone-0.32h; do
	cp -R "$S/deps/$d" "$OUT.new/deps/"
done
cp "$S/test/testcases/testcases.json" "$OUT.new/test/testcases/"

# not needed for a static library build
rm -rf "$OUT.new/src/pyd" "$OUT.new/src/py_opencc.cpp" "$OUT.new/src/benchmark" "$OUT.new/data/icon" \
	"$OUT.new/deps/marisa-0.3.1/bindings" "$OUT.new/deps/marisa-0.3.1/docs" \
	"$OUT.new/deps/marisa-0.3.1/tests" "$OUT.new/deps/marisa-0.3.1/tools" "$OUT.new/deps/marisa-0.3.1/.github" \
	"$OUT.new/deps/darts-clone-0.32h/MODULE.bazel"
find "$OUT.new" -name BUILD.bazel -delete

rm -rf "$OUT"
mv "$OUT.new" "$OUT"
echo "vendored OpenCC $VERSION into ${OUT#$ROOT/} ($(du -sh "$OUT" | cut -f1), $(find "$OUT" -type f | wc -l | tr -d ' ') files)"

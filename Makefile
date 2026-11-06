# pg_opencc: the opencc extension for PostgreSQL
#
#   make && sudo make install && make installcheck
#
# Build requirements: pg_config, make, cmake >= 3.12, a C++17 compiler and
# python3.  OpenCC is vendored under vendor/opencc and linked statically; its
# dictionaries are compiled on the build host and embedded into opencc.so, so
# nothing else is needed at runtime.

EXTENSION  = opencc
EXTVERSION = 0.1.0
MODULE_big = opencc
OBJS       = src/opencc_pg.o
DATA       = sql/opencc--$(EXTVERSION).sql
PGFILEDESC = "opencc - OpenCC Chinese conversion for PostgreSQL"

REGRESS      = opencc_convert opencc_localize opencc_inspect opencc_ops opencc_upstream
REGRESS_OPTS = --inputdir=test --outputdir=build/test

PG_CONFIG ?= pg_config
CMAKE     ?= cmake
OPENCC_PYTHON ?= python3

OPENCC_SRC     = vendor/opencc
OPENCC_BUILD   = build/opencc
OPENCC_STAMP   = $(OPENCC_BUILD)/.built
OPENCC_LIBS    = $(OPENCC_BUILD)/src/libopencc.a $(OPENCC_BUILD)/deps/marisa-0.3.1/libmarisa.a
OPENCC_CONFIGS = s2t t2s s2tw tw2s s2hk hk2s s2twp tw2sp s2hkp hk2sp t2tw tw2t t2hk hk2t t2jp jp2t
CORE_OBJS      = build/opencc_core.o build/opencc_resources.o

PG_CPPFLAGS = -Isrc -DOPENCC_PG_VERSION='"$(EXTVERSION)"'
SHLIB_LINK  = $(CORE_OBJS) $(OPENCC_LIBS) $(CXX_RUNTIME) $(EXPORT_FLAGS)
EXTRA_CLEAN = build

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

OPENCC_CXX ?= $(CXX)

ifeq ($(PORTNAME),darwin)
CXX_RUNTIME  = -lc++
EXPORT_FLAGS =
else
CXX_RUNTIME  = -lstdc++
# export only what PostgreSQL looks up; keep OpenCC and C++ symbols private
EXPORT_FLAGS = -Wl,--exclude-libs,ALL -Wl,--version-script=$(CURDIR)/src/exports.map
endif

OPENCC_CXXFLAGS = -std=c++17 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
	-DOpencc_BUILT_AS_STATIC -I$(OPENCC_SRC)/src -I$(OPENCC_BUILD)/src -Isrc

# ---------------------------------------------------------------------------
# vendored OpenCC: static libraries plus .ocd2 dictionaries for this host
# ---------------------------------------------------------------------------
$(OPENCC_BUILD)/Makefile:
	$(CMAKE) -S $(OPENCC_SRC) -B $(OPENCC_BUILD) -G "Unix Makefiles" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_CXX_COMPILER=$(OPENCC_CXX) \
		-DPython3_EXECUTABLE=$(shell command -v $(OPENCC_PYTHON)) \
		-DBUILD_SHARED_LIBS=OFF \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
		-DCMAKE_CXX_VISIBILITY_PRESET=hidden \
		-DCMAKE_VISIBILITY_INLINES_HIDDEN=ON \
		-DOPENCC_ENABLE_INSTALL=OFF \
		-DBUILD_OPENCC_JIEBA_PLUGIN=OFF \
		-DBUILD_DOCUMENTATION=OFF \
		-DBUILD_PYTHON=OFF \
		-DENABLE_GTEST=OFF \
		-DENABLE_BENCHMARK=OFF \
		-DCMAKE_DISABLE_FIND_PACKAGE_Git=TRUE

$(OPENCC_STAMP): $(OPENCC_BUILD)/Makefile
	+$(MAKE) -C $(OPENCC_BUILD)
	touch $@

$(OPENCC_LIBS): $(OPENCC_STAMP)

build/opencc_resources.c: $(OPENCC_STAMP) tools/embed_resources.py
	$(OPENCC_PYTHON) tools/embed_resources.py \
		--config-dir $(OPENCC_SRC)/data/config \
		--dict-dir $(OPENCC_BUILD)/data \
		--version-cmake $(OPENCC_SRC)/cmake/GitVersion.cmake \
		--output $@ $(OPENCC_CONFIGS)

build/opencc_resources.o: build/opencc_resources.c src/opencc_resources.h
	$(CC) $(CFLAGS) $(CFLAGS_SL) -fPIC -fvisibility=hidden -Isrc -c -o $@ $<

build/opencc_core.o: src/opencc_core.cpp src/opencc_core.h src/opencc_resources.h $(OPENCC_STAMP)
	$(OPENCC_CXX) $(CXXFLAGS) $(OPENCC_CXXFLAGS) -c -o $@ $<

src/opencc_pg.o: src/opencc_core.h src/opencc_resources.h

$(MODULE_big)$(DLSUFFIX): $(CORE_OBJS) $(OPENCC_LIBS) src/exports.map

.PHONY: opencc
opencc: $(OPENCC_STAMP)

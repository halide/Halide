HALIDE_DISTRIB_PATH ?= $(realpath ../../distrib)
LDFLAGS ?=
IMAGES ?= ../images
UNAME ?= $(shell uname)
SHELL = bash

PYTHON ?= python3

# TODO(srj): the python bindings need to be put into the distrib folders;
# this is a hopefully-temporary workaround (https://github.com/halide/Halide/issues/4368)
HALIDE_PYTHON_BINDINGS_PATH ?= $(realpath ../../bin/python3_bindings)

BIN_DIR ?= bin

# Most build outputs go into $(BIN)/$(HL_TARGET)/$(HL_TARGET)/, so that you can vary the test
# and/or benchmark output easily by changing HL_TARGET and not have to worry
# about cleaning the output (e.g. to add different GPU or SIMD features).
BIN ?= $(BIN_DIR)
HL_TARGET ?= host


# GENERATOR_BIN is used mainly for Generators, which are always built
# (and executed) on the host, regardless of the final target; this saves a bit
# of recompile time (and disk space). (It would still provide correct results
# if we output them to $(BIN), it would just be inefficient.)
GENERATOR_BIN ?= $(BIN)/host

SANITIZER_FLAGS ?=

# The outputs to produce when running a Generator (ie the args to the -e flag).
# If -e is unspecified, Halide normally defaults to the equivalent of
# `-e static_library,h,registration`; we add assembly and stmt to the 'default'
# outputs for the apps, since it makes casual inspection of the Generator output
# easier to inspect and experiment with. (Production build systems wouldn't
# normally do this, as it would just waste cycles and storage.)
GENERATOR_OUTPUTS ?= static_library,h,registration,stmt,assembly

# This pulls in the definition of HALIDE_SYSTEM_LIBS and HALIDE_RTTI
include $(HALIDE_DISTRIB_PATH)/halide_config.make

ifeq ($(OS), Windows_NT)
# Flags for mingw environment
LDFLAGS += -lz -lole32 -lkernel32 -lpsapi -luuid -Wl,--stack,8388608
else
LDFLAGS += -ldl -lpthread -lz
endif

CXX ?= g++
GXX ?= g++

OPTIMIZE ?= -O3

CFLAGS += $(OPTIMIZE) -I $(HALIDE_DISTRIB_PATH)/include/ -I $(HALIDE_DISTRIB_PATH)/tools/ -I $(HALIDE_DISTRIB_PATH)/apps/support/
CXXFLAGS += $(OPTIMIZE) -std=c++11 -I $(HALIDE_DISTRIB_PATH)/include/ -I $(HALIDE_DISTRIB_PATH)/tools/ $(SANITIZER_FLAGS) -Wall -Werror -Wno-unused-function -Wcast-qual -Wignored-qualifiers -Wno-comment -Wsign-compare -Wno-unknown-warning-option -Wno-psabi

CXX_VERSION = $(shell $(CXX) --version | head -n1)
ifneq (,$(findstring clang,$(CXX_VERSION)))
CXXFLAGS += $(findstring -stdlib=libc++, $(HALIDE_LLVM_CXX_FLAGS))
endif

ifeq (0, $(HALIDE_RTTI))
CXXFLAGS += -fno-rtti
else
CXXFLAGS += -frtti
endif

ifeq ($(UNAME), Darwin)
CXXFLAGS += -fvisibility=hidden
endif

ifeq ($(UNAME), Linux)
USE_EXPORT_DYNAMIC=-rdynamic
else
ifeq ($(UNAME), Darwin)
USE_EXPORT_DYNAMIC=-undefined dynamic_lookup
else
USE_EXPORT_DYNAMIC=
endif
endif

ifeq ($(OS), Windows_NT)
SHARED_EXT=dll
else
ifeq ($(UNAME), Darwin)
SHARED_EXT=dylib
else
SHARED_EXT=so
endif
endif

# To run apps on android that support this, build a separate toolchain from the Android NDK
# using the make-standalone-toolchain.sh script:
#$ build/tools/make-standalone-toolchain.sh --arch=arm64 --platform=android-21 --install-dir=$ANDROID_ARM64_TOOLCHAIN
#$ build/tools/make-standalone-toolchain.sh --arch=arm --platform=android-21 --install-dir=$ANDROID_ARM_TOOLCHAIN
CXX-host ?= $(CXX)
CXX-host-opencl ?= $(CXX)
CXX-host-opengl ?= $(CXX)
CXX-host-cuda ?= $(CXX)
CXX-host-metal ?= $(CXX)
CXX-host-hvx_128 ?= $(CXX)
CXX-host-hvx_64 ?= $(CXX)
CXX-$(HL_TARGET) ?= $(CXX)
CXX-arm-64-android ?= $(ANDROID_ARM64_TOOLCHAIN)/bin/aarch64-linux-android-c++
CXX-arm-32-android ?= $(ANDROID_ARM_TOOLCHAIN)/bin/arm-linux-androideabi-c++

CXXFLAGS-host ?= $(CXXFLAGS)
CXXFLAGS-host-opencl ?= $(CXXFLAGS)
CXXFLAGS-host-opengl ?= $(CXXFLAGS)
CXXFLAGS-host-cuda ?= $(CXXFLAGS)
CXXFLAGS-host-metal ?= $(CXXFLAGS)
CXXFLAGS-host-hvx_128 ?= $(CXXFLAGS)
CXXFLAGS-host-hvx_64 ?= $(CXXFLAGS)
CXXFLAGS-$(HL_TARGET) ?= $(CXXFLAGS)
CXXFLAGS-arm-64-android ?= $(CXXFLAGS)
CXXFLAGS-arm-32-android ?= $(CXXFLAGS)

LDFLAGS-host ?= $(LDFLAGS)
LDFLAGS-host-opencl ?= $(LDFLAGS)
LDFLAGS-host-opengl ?= $(LDFLAGS)
LDFLAGS-host-cuda ?= $(LDFLAGS)
LDFLAGS-host-metal ?= $(LDFLAGS)
LDFLAGS-host-hvx_128 ?= $(LDFLAGS)
LDFLAGS-host-hvx_64 ?= $(LDFLAGS)
LDFLAGS-$(HL_TARGET) ?= $(LDFLAGS)
LDFLAGS-arm-64-android ?= -llog -fPIE -pie
LDFLAGS-arm-32-android ?= -llog -fPIE -pie

ifeq ($(OS), Windows_NT)

# TODO: copy libHalide.dll somewhere that windows can find it, and use it instead
LIB_HALIDE = $(HALIDE_DISTRIB_PATH)/lib/libHalide.a

else

LIB_HALIDE = $(HALIDE_DISTRIB_PATH)/bin/libHalide.$(SHARED_EXT)

endif

GENERATOR_DEPS ?= $(LIB_HALIDE) $(HALIDE_DISTRIB_PATH)/include/Halide.h $(HALIDE_DISTRIB_PATH)/tools/GenGen.cpp

# Generators which use autoscheduler plugin need to specify the linker where to find libHalide.so required by the plugin.
GENERATOR_LDFLAGS ?= -Wl,-rpath,$(dir $(LIB_HALIDE)) -L $(dir $(LIB_HALIDE)) -lHalide $(LDFLAGS)

LIBPNG_LIBS_DEFAULT = $(shell libpng-config --ldflags)
LIBPNG_CXX_FLAGS ?= $(shell libpng-config --cflags)
# Workaround for libpng-config pointing to 64-bit versions on linux even when we're building for 32-bit
ifneq (,$(findstring -m32,$(CXX)))
ifneq (,$(findstring x86_64,$(LIBPNG_LIBS_DEFAULT)))
LIBPNG_LIBS ?= -lpng
endif
endif
LIBPNG_LIBS ?= $(LIBPNG_LIBS_DEFAULT)

# Workaround brew Cellar path for libpng-config output.
LIBJPEG_LINKER_PATH ?= $(shell echo $(LIBPNG_LIBS_DEFAULT) | sed -e'/-L.*[/][Cc]ellar[/]libpng/!d;s=\(.*\)/[Cc]ellar/libpng/.*=\1/lib=')
LIBJPEG_LIBS ?= $(LIBJPEG_LINKER_PATH) -ljpeg

# There's no libjpeg-config, unfortunately. We should look for
# jpeglib.h one directory level up from png.h . Also handle
# Mac OS brew installs where libpng-config returns paths
# into the PNG cellar.
LIBPNG_INCLUDE_DIRS = $(filter -I%,$(LIBPNG_CXX_FLAGS))
LIBJPEG_CXX_FLAGS ?= $(shell echo $(LIBPNG_INCLUDE_DIRS) | sed -e'/[Cc]ellar[/]libpng/!s=\(.*\)=\1/..=;s=\(.*\)/[Cc]ellar/libpng/.*=\1/include=')

IMAGE_IO_LIBS = $(LIBPNG_LIBS) $(LIBJPEG_LIBS)
IMAGE_IO_CXX_FLAGS = $(LIBPNG_CXX_FLAGS) $(LIBJPEG_CXX_FLAGS)

IMAGE_IO_FLAGS = $(IMAGE_IO_LIBS) $(IMAGE_IO_CXX_FLAGS)

PLATFORM_OPENGL_LDFLAGS=-lGL -lX11
ifeq ($(UNAME), Darwin)
PLATFORM_OPENGL_LDFLAGS=-framework OpenGL
endif

ifneq (, $(findstring opengl,$(HL_TARGET)))
  OPENGL_LDFLAGS=$(PLATFORM_OPENGL_LDFLAGS)
endif

ifneq (, $(findstring metal,$(HL_TARGET)))
  LDFLAGS += -framework Metal -framework Foundation
endif

# Utility to convert raw video -> h264. HL_AVCONV=ffmpeg will work too.
HL_AVCONV ?= avconv

# Utility to show h264 video
HL_VIDEOPLAYER ?= mplayer

# ------- RunGen/Benchmark support

# Really, .SECONDARY is what we want instead of .PRECIOUS (here and elsewhere),
# but .SECONDARY doesn't accept wildcards
.PRECIOUS: $(BIN)/%.registration.cpp
$(BIN)/%.registration.cpp: $(BIN)/%.a
	@echo $@ produced implicitly by $^

.PRECIOUS: $(BIN)/%/RunGenMain.o
$(BIN)/%/RunGenMain.o: $(HALIDE_DISTRIB_PATH)/tools/RunGenMain.cpp $(HALIDE_DISTRIB_PATH)/tools/RunGen.h
	@[[ "$(HL_TARGET)" != "wasm-32-wasmrt"* ]] || (echo "HL_TARGET must NOT begin with wasm-32-wasmrt for target $@" && exit 1)
	@mkdir -p $(@D)
	$(CXX) -c $< $(CXXFLAGS) -fno-exceptions $(IMAGE_IO_CXX_FLAGS) -I$(BIN)/$* -o $@

.PRECIOUS: $(BIN)/%.rungen
$(BIN)/%.rungen: $(BIN)/%/RunGenMain.o $(BIN)/%.a $(BIN)/%.registration.cpp
	@[[ "$(HL_TARGET)" != "wasm-32-wasmrt"* ]] || (echo "HL_TARGET must NOT begin with wasm-32-wasmrt for target $@" && exit 1)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

RUNARGS ?=

# Pseudo target that allows us to build-and-run in one step, e.g.
#
#     make bin/host/foo.run RUNARGS='input=a output=baz'
#
.PHONY: $(BIN)/%.run
$(BIN)/%.run: $(BIN)/%.rungen
	@$(CURDIR)/$< $(RUNARGS)

# Also allow 'foo.run' as a shortcut to mean host
.PHONY: %.run
%.run: $(BIN)/host/%.rungen
	@$(CURDIR)/$< $(RUNARGS)

.PHONY: %.benchmark
%.benchmark: $(BIN)/$(HL_TARGET)/%.rungen
	@[[ "$(HL_TARGET)" != "wasm-32-wasmrt"* ]] || (echo "HL_TARGET must NOT begin with wasm-32-wasmrt for target $@" && exit 1)
	@$^ --benchmarks=all --estimate_all --parsable_output

# ------- wasm support

# EMCC is the tool that invokes Emscripten
EMCC ?= emcc

# WASM_SHELL is the shell tool used to run AOT-compiled WebAssembly.
# (Node could be used instead.)
WASM_SHELL ?= d8

ifneq (,$(findstring wasm_simd128,$(HL_TARGET)))
	EMCC_SIMD_OPT=1
	WASM_SHELL += --experimental-wasm-simd
else
	EMCC_SIMD_OPT=0
endif

ifneq (,$(findstring node,$(WASM_SHELL)))
	EMCC_ENVIRONMENT=node
	WASM_SHELL_ARG_SEP=
else
	EMCC_ENVIRONMENT=shell
	WASM_SHELL_ARG_SEP="--"
endif

# We slurp in the default ~/.emscripten config file and make an altered version
# with LLVM_ROOT pointing to the LLVM we are using, so that we can build with
# the 'correct' version (ie the one that the rest of Halide is using) whether
# or not ~/.emscripten has been edited correctly.
LLVM_CONFIG ?= llvm-config
LLVM_BINDIR = $(shell $(LLVM_CONFIG) --bindir | sed -e 's/\\/\//g' -e 's/\([a-zA-Z]\):/\/\1/g')
EMCC_CONFIG = $(shell cat $(HOME)/.emscripten | grep -v LLVM_ROOT | tr '\n' ';')LLVM_ROOT='$(LLVM_BINDIR)'

# Note that USE_LIBJPEG requires Emscripten 1.38.31 or later
EMCC_OPTS = \
	--source-map-base . \
	-s ALLOW_MEMORY_GROWTH=1 \
	-s ASSERTIONS=1 \
	-s ENVIRONMENT=$(EMCC_ENVIRONMENT) \
	-s EXIT_RUNTIME=1 \
	-s FORCE_FILESYSTEM=1 \
	-s SIMD=$(EMCC_SIMD_OPT) \
	-s USE_LIBPNG=1 \
	-s USE_LIBJPEG=1 \
	-s WASM=1

CXX-WASM = EMCC_WASM_BACKEND=1 EM_CONFIG="$(EMCC_CONFIG)" $(EMCC) $(EMCC_OPTS)

.PRECIOUS: $(BIN)/%/RunGenMainWasm.o
$(BIN)/%/RunGenMainWasm.o: $(HALIDE_DISTRIB_PATH)/tools/RunGenMain.cpp $(HALIDE_DISTRIB_PATH)/tools/RunGen.h
	@[[ "$(HL_TARGET)" == "wasm-32-wasmrt"* ]] || (echo "HL_TARGET must begin with wasm-32-wasmrt" && exit 1)
	@mkdir -p $(@D)
	$(CXX-WASM) -c $< $(CXXFLAGS) -fno-exceptions -I$(BIN)/$* -o $@

.PRECIOUS: $(BIN)/%.rungen.js
$(BIN)/%.rungen.js: $(BIN)/%/RunGenMainWasm.o $(BIN)/%.a $(BIN)/%.registration.cpp
	@[[ "$(HL_TARGET)" == "wasm-32-wasmrt"* ]] || (echo "HL_TARGET must begin with wasm-32-wasmrt for target $@" && exit 1)
	$(CXX-WASM) $(CXXFLAGS) -g $^ -o $@

.PHONY: $(BIN)/%.rungen_wasm
$(BIN)/%.rungen_wasm: $(BIN)/%.rungen.js
	# nothing

.PHONY: %.benchmark_wasm
%.benchmark_wasm: $(BIN)/$(HL_TARGET)/%.rungen.js
	@[[ "$(HL_TARGET)" == "wasm-32-wasmrt"* ]] || (echo "HL_TARGET must begin with wasm-32-wasmrt for target $@" && exit 1)
	@cd $(<D); $(WASM_SHELL) $(<F) $(WASM_SHELL_ARG_SEP) --benchmarks=all --estimate_all --parsable_output

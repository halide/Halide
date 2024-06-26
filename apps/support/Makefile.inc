# Disable built-in makefile rules for all apps to avoid pointless file-system
# scanning and general weirdness resulting from implicit rules.
MAKEFLAGS += --no-builtin-rules
.SUFFIXES:

HALIDE_DISTRIB_PATH ?= $(realpath ../../distrib)
ifeq (,$(HALIDE_DISTRIB_PATH))
$(error Could not find a Halide distribution. Before building an app, run "make distrib" at the root level, or set HALIDE_DISTRIB_PATH to the root of an existing Halide distribution)
endif

LDFLAGS ?=
IMAGES ?= ../images
UNAME ?= $(shell uname)
SHELL = bash
PYTHON ?= python3
BIN_DIR ?= bin

# Most build outputs go into $(BIN)/$(HL_TARGET)/$(HL_TARGET)/, so that you can vary the test
# and/or benchmark output easily by changing HL_TARGET and not have to worry
# about cleaning the output (e.g. to add different GPU or SIMD features).
BIN ?= $(BIN_DIR)
HL_TARGET ?= host

# Running examples on Hexagon requires Hexagon_SDK
SDK_ROOT ?= ${HOME}/Qualcomm
HEXAGON_SDK_ROOT ?= ${SDK_ROOT}/Hexagon_SDK/4.0.0

# GENERATOR_BIN is used mainly for Generators, which are always built
# (and executed) on the host, regardless of the final target; this saves a bit
# of recompile time (and disk space). (It would still provide correct results
# if we output them to $(BIN), it would just be inefficient.)
GENERATOR_BIN ?= $(BIN)/host

# The outputs to produce when running a Generator (ie the args to the -e flag).
# If -e is unspecified, Halide normally defaults to the equivalent of
# `-e static_library,h,registration`; we add assembly and stmt to the 'default'
# outputs for the apps, since it makes casual inspection of the Generator output
# easier to inspect and experiment with. (Production build systems wouldn't
# normally do this, as it would just waste cycles and storage.)
GENERATOR_OUTPUTS ?= static_library,h,registration,stmt,assembly

# This pulls in the definition of HALIDE_SYSTEM_LIBS and HALIDE_RTTI
include $(HALIDE_DISTRIB_PATH)/halide_config.make

LDFLAGS += -ldl -lpthread -lz

CXX ?= g++
GXX ?= g++

OPTIMIZE ?= -O3

SANITIZER_FLAGS ?=

ifneq (,$(findstring tsan,$(HL_TARGET)$(HL_JIT_TARGET)))
# Note that attempting to use TSAN with the JIT can produce false positives
# if libHalide is not also compiled with TSAN enabled; we tack the relevant
# flag onto OPTIMIZE here, but that's really only effective if you ensure
# to do a clean build before testing. (In general, most of the Sanitizers
# only work well when used in a completely clean environment.)
OPTIMIZE += -fsanitize=thread
SANITIZER_FLAGS += -fsanitize=thread
endif

ifneq (,$(findstring asan,$(HL_TARGET)$(HL_JIT_TARGET)))
OPTIMIZE += -fsanitize=address
SANITIZER_FLAGS += -fsanitize=address
endif

CFLAGS += $(OPTIMIZE) -I $(HALIDE_DISTRIB_PATH)/include/ -I $(HALIDE_DISTRIB_PATH)/tools/ -I $(HALIDE_DISTRIB_PATH)/apps/support/
CXXFLAGS += $(OPTIMIZE) -std=c++17 -I $(HALIDE_DISTRIB_PATH)/include/ -I $(HALIDE_DISTRIB_PATH)/tools/ $(SANITIZER_FLAGS) -Wall -Werror -Wno-unused-function -Wcast-qual -Wignored-qualifiers -Wno-comment -Wsign-compare -Wno-unknown-warning-option -Wno-psabi

CXX_VERSION = $(shell $(CXX) --version | head -n1)
ifneq (,$(findstring clang,$(CXX_VERSION)))
CXXFLAGS += $(findstring -stdlib=libc++, $(HALIDE_LLVM_CXX_FLAGS))
endif

ifeq (0, $(HALIDE_RTTI))
CXXFLAGS += -fno-rtti
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

ifeq ($(UNAME), Darwin)
SHARED_EXT=dylib
else
SHARED_EXT=so
endif

# We want to build Halide plugins as .so on all posixy systems, including OSX.
# This is called out as a named var to make it clear that the use
# is deliberate, not an accident.
PLUGIN_EXT=so

# We expect $ANDROID_NDK_ROOT to be defined by an env var.
# We require at least NDK r19b or later.
ANDROID_NDK_ROOT ?= /path/to/android_ndk_root

# e.g. 'darwin' or 'linux'
ANDROID_NDK_HOST_PLATFORM ?= $(shell echo `uname` | tr '[:upper:]' '[:lower:]')

# The minimum Android API version to compile against. (This is *not* the same as the NDK version.)
# 26 - Android 8 (aka Oreo)
ANDROID_API_VERSION ?= 26

CXX-host ?= $(CXX)
CXX-host-opencl ?= $(CXX)
CXX-host-cuda ?= $(CXX)
CXX-host-metal ?= $(CXX)
CXX-host-hvx ?= $(CXX)
CXX-hexagon-32-qurt-hvx ?= hexagon-clang++
CXX-arm-64-android ?= ${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${ANDROID_NDK_HOST_PLATFORM}-x86_64/bin/aarch64-linux-android${ANDROID_API_VERSION}-clang++
CXX-arm-64-android-arm_dot_prod ?= $(CXX-arm-64-android)
CXX-arm-32-android ?= ${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${ANDROID_NDK_HOST_PLATFORM}-x86_64/bin/armv7a-linux-androideabi${ANDROID_API_VERSION}-clang++
CXX-arm-64-profile-android ?= $(CXX-arm-64-android)
CXX-arm-32-profile-android ?= $(CXX-arm-32-android)

CXXFLAGS-host ?= $(CXXFLAGS)
CXXFLAGS-host-opencl ?= $(CXXFLAGS)
CXXFLAGS-host-cuda ?= $(CXXFLAGS)
CXXFLAGS-host-metal ?= $(CXXFLAGS)
CXXFLAGS-arm-64-android ?= $(CXXFLAGS)
CXXFLAGS-arm-64-android-arm_dot_prod ?= $(CXXFLAGS-arm-64-android)
CXXFLAGS-hexagon-32-qurt-hvx ?= -mv65 $(CXXFLAGS) -I$(HEXAGON_SDK_ROOT)/rtos/qurt/computev65/include/qurt -I$(HEXAGON_SDK_ROOT)/rtos/qurt/computev65/include/posix
CXXFLAGS-arm-32-android ?= $(CXXFLAGS)

AR-host ?= ar
AR-host-debug ?= ar
AR-host-opencl ?= $(AR)
AR-host-cuda ?= $(AR)
AR-host-metal ?= $(AR)
AR-host-hvx ?= $(AR)
AR-hexagon-32-qurt-hvx ?= ar
AR-arm-64-android ?= ${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${ANDROID_NDK_HOST_PLATFORM}-x86_64/bin/llvm-ar
AR-arm-64-android-arm_dot_prod ?= $(AR-arm-64-android)
AR-arm-32-android ?= ${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/${ANDROID_NDK_HOST_PLATFORM}-x86_64/bin/armv7a-linux-androideabi-ar
AR-arm-64-profile-android ?= $(AR-arm-64-android)
AR-arm-32-profile-android ?= $(AR-arm-32-android)

LDFLAGS-host ?= $(LDFLAGS)
LDFLAGS-host-opencl ?= $(LDFLAGS)
LDFLAGS-host-cuda ?= $(LDFLAGS)
LDFLAGS-host-metal ?= $(LDFLAGS)
LDFLAGS-host-hvx ?= $(LDFLAGS)
LDFLAGS-hexagon-32-qurt-hvx =
# Use the statically-linked version of libc++ on Android by default, for simplicity
# of deployment. (Despite the name, this applies to libc++, not libstdc++)
LDFLAGS-arm-64-android ?= -llog -fPIE -pie -static-libstdc++
LDFLAGS-arm-64-android-arm_dot_prod ?= $(LDFLAGS-arm-64-android)
LDFLAGS-arm-32-android ?= -llog -fPIE -pie -static-libstdc++

# Put HL_TARGET variants of all of these last, to avoid conflict with the android variants
# if building with `HL_TARGET=arm-64-android make foo`
CXX-$(HL_TARGET) ?= $(CXX)
CXXFLAGS-$(HL_TARGET) ?= $(CXXFLAGS)
LDFLAGS-$(HL_TARGET) ?= $(LDFLAGS)

LIB_HALIDE_STATIC = $(HALIDE_DISTRIB_PATH)/lib/libHalide.a

LIB_HALIDE = $(HALIDE_DISTRIB_PATH)/lib/libHalide.$(SHARED_EXT)

GENERATOR_DEPS ?= $(LIB_AUTOSCHEDULER) $(LIB_HALIDE) $(HALIDE_DISTRIB_PATH)/include/Halide.h $(HALIDE_DISTRIB_PATH)/tools/GenGen.cpp
GENERATOR_DEPS_STATIC ?= $(LIB_HALIDE_STATIC) $(HALIDE_DISTRIB_PATH)/include/Halide.h $(HALIDE_DISTRIB_PATH)/tools/GenGen.cpp

# Generators which use autoscheduler plugin need to specify the linker where to find libHalide.so required by the plugin.
LIBHALIDE_LDFLAGS ?= -Wl,-rpath,$(dir $(LIB_HALIDE)) -L $(dir $(LIB_HALIDE)) -lHalide $(LDFLAGS)
LIBHALIDE_LDFLAGS_STATIC ?= $(LIB_HALIDE_STATIC) $(LDFLAGS)

# Autoschedulers. Mullapudi2016 is currently the default, because it's fast.
AUTOSCHEDULER ?= mullapudi2016
ifneq ($(AUTOSCHEDULER),)
LIB_AUTOSCHEDULER ?= $(HALIDE_DISTRIB_PATH)/lib/libautoschedule_$(AUTOSCHEDULER).$(PLUGIN_EXT)
ifeq ($(UNAME), Darwin)
LIBHALIDE_LDFLAGS +=  -Wl,-force_load $(HALIDE_DISTRIB_PATH)/lib/libautoschedule_$(AUTOSCHEDULER).$(PLUGIN_EXT)
else
LIBHALIDE_LDFLAGS +=  -Wl,--no-as-needed -lautoschedule_$(AUTOSCHEDULER) -Wl,--as-needed
endif
endif

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
	@mkdir -p $(@D)
	$(CXX) -c $< $(CXXFLAGS) -fno-exceptions $(IMAGE_IO_CXX_FLAGS) -I$(BIN)/$* -o $@

.PRECIOUS: $(BIN)/%.rungen
$(BIN)/%.rungen: $(BIN)/%/RunGenMain.o $(BIN)/%.a $(BIN)/%.registration.cpp
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
	@$^ --benchmarks=all --estimate_all --parsable_output


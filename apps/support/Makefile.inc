HALIDE_DISTRIB_PATH ?= ../../distrib
LDFLAGS ?=
BIN ?= ./bin
IMAGES ?= ../images
HL_TARGET ?= host
UNAME ?= $(shell uname)

SANITIZER_FLAGS ?=

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

CFLAGS += -I $(HALIDE_DISTRIB_PATH)/include/ -I $(HALIDE_DISTRIB_PATH)/tools/ -I $(HALIDE_DISTRIB_PATH)/apps/support/
CXXFLAGS += -std=c++11 -I $(HALIDE_DISTRIB_PATH)/include/ -I $(HALIDE_DISTRIB_PATH)/tools/ $(SANITIZER_FLAGS) -Wall -Werror -Wno-unused-function -Wcast-qual -Wignored-qualifiers -Wno-comment -Wsign-compare -Wno-unknown-warning-option -Wno-psabi

ifeq (0, $(HALIDE_RTTI))
CXXFLAGS += -fno-rtti
endif

ifeq ($(UNAME), Darwin)
CXXFLAGS += -fvisibility=hidden
endif

ifeq ($(UNAME), Linux)
USE_EXPORT_DYNAMIC=-rdynamic
USE_OPEN_MP=-fopenmp
else
ifeq ($(UNAME), Darwin)
USE_EXPORT_DYNAMIC=-undefined dynamic_lookup
USE_OPEN_MP=
else
USE_EXPORT_DYNAMIC=
USE_OPEN_MP=
endif
endif


# To run apps on android that support this, build a separate toolchain from the Android NDK
# using the make-standalone-toolchain.sh script:
#$ build/tools/make-standalone-toolchain.sh --arch=arm64 --platform=android-21 --install-dir=$ANDROID_ARM64_TOOLCHAIN
#$ build/tools/make-standalone-toolchain.sh --arch=arm --platform=android-21 --install-dir=$ANDROID_ARM_TOOLCHAIN
CXX-host ?= $(CXX)
CXX-arm-64-android ?= $(ANDROID_ARM64_TOOLCHAIN)/bin/aarch64-linux-android-c++
CXX-arm-32-android ?= $(ANDROID_ARM_TOOLCHAIN)/bin/arm-linux-androideabi-c++

CXXFLAGS-host ?= $(CXXFLAGS)
CXXFLAGS-arm-64-android ?= $(CXXFLAGS)
CXXFLAGS-arm-32-android ?= $(CXXFLAGS)

LDFLAGS-host ?= $(LDFLAGS)
LDFLAGS-arm-64-android ?= -llog -fPIE -pie
LDFLAGS-arm-32-android ?= -llog -fPIE -pie

LIB_HALIDE = $(HALIDE_DISTRIB_PATH)/lib/libHalide.a

GENERATOR_DEPS ?= $(HALIDE_DISTRIB_PATH)/lib/libHalide.a $(HALIDE_DISTRIB_PATH)/include/Halide.h $(HALIDE_DISTRIB_PATH)/tools/GenGen.cpp

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

$(BIN)/%.registration.cpp: $(BIN)/%.a
	@echo $@ produced implicitly by $^

$(BIN)/RunGenMain.o: $(HALIDE_DISTRIB_PATH)/tools/RunGenMain.cpp $(HALIDE_DISTRIB_PATH)/tools/RunGen.h
	@mkdir -p $(@D)
	@$(CXX) -c $< $(CXXFLAGS) $(IMAGE_IO_CXX_FLAGS) -I$(BIN) -o $@

# Really, .SECONDARY is what we want, but it won't accept wildcards
.PRECIOUS: $(BIN)/%.rungen
$(BIN)/%.rungen: $(BIN)/RunGenMain.o $(BIN)/%.a $(BIN)/%.registration.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

RUNARGS ?=

# Pseudo target that allows us to build-and-run in one step, e.g.
#
#     make foo.run RUNARGS='input=a output=baz'
#
$(BIN)/%.run: $(BIN)/%.rungen
	@$(CURDIR)/$< $(RUNARGS)

# Utility to convert raw video -> h264. HL_AVCONV=ffmpeg will work too.
HL_AVCONV ?= avconv

# Utility to show h264 video
HL_VIDEOPLAYER ?= mplayer


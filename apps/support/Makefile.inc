HALIDE_PATH ?= ../..

CXX ?= g++
GXX ?= g++

CFLAGS += -I $(HALIDE_PATH)/include/ -I $(HALIDE_PATH)/tools/ -I../support/
CXXFLAGS += -std=c++11 -I $(HALIDE_PATH)/include/ -I $(HALIDE_PATH)/tools/ -I../support/

LIB_HALIDE = $(HALIDE_PATH)/bin/libHalide.a

GENERATOR_DEPS ?= $(HALIDE_PATH)/bin/libHalide.a $(HALIDE_PATH)/include/Halide.h ../../tools/GenGen.cpp

LLVM_CONFIG ?= llvm-config
LLVM_VERSION_TIMES_10 = $(shell $(LLVM_CONFIG) --version | cut -b 1,3)
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags --system-libs)

LLVM_CONFIG ?= llvm-config
LLVM_FULL_VERSION = $(shell $(LLVM_CONFIG) --version)
LLVM_LIBDIR = $(shell $(LLVM_CONFIG) --libdir)

ifeq ($(USE_LLVM_SHARED_LIB), )
LLVM_STATIC_LIBS = -L $(LLVM_LIBDIR) $(shell $(LLVM_CONFIG) --libs bitwriter bitreader linker ipo mcjit $(LLVM_OLD_JIT_COMPONENT) $(X86_LLVM_CONFIG_LIB) $(ARM_LLVM_CONFIG_LIB) $(OPENCL_LLVM_CONFIG_LIB) $(NATIVE_CLIENT_LLVM_CONFIG_LIB) $(PTX_LLVM_CONFIG_LIB) $(AARCH64_LLVM_CONFIG_LIB) $(MIPS_LLVM_CONFIG_LIB))
LLVM_SHARED_LIBS =
else
LLVM_STATIC_LIBS =
LLVM_SHARED_LIBS = -L $(LLVM_LIBDIR) -lLLVM-$(LLVM_FULL_VERSION)
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
PNGFLAGS=$(LIBPNG_LIBS) $(LIBPNG_CXX_FLAGS)

UNAME = $(shell uname)

PLATFORM_OPENGL_LDFLAGS=-lGL -lX11
ifeq ($(UNAME), Darwin)
PLATFORM_OPENGL_LDFLAGS=-framework OpenGL
endif

ifneq (, $(findstring opengl,$(HL_TARGET)))
  OPENGL_LDFLAGS=$(PLATFORM_OPENGL_LDFLAGS)
endif

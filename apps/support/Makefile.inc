CXX ?= g++
GXX ?= g++

CFLAGS += -I../../include/ -I../support/
CXXFLAGS += -I../../include/ -I../support/

ifdef BUILD_PREFIX
LIB_HALIDE = bin/$(BUILD_PREFIX)/libHalide.a
else
LIB_HALIDE = bin/libHalide.a
endif

LLVM_CONFIG ?= llvm-config
LLVM_VERSION_TIMES_10 = $(shell $(LLVM_CONFIG) --version | cut -b 1,3)
LLVM_34_OR_OLDER = $(findstring $(LLVM_VERSION_TIMES_10), 32 33 34)
ifneq ($(LLVM_34_OR_OLDER), )
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags)
else
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags --system-libs)
endif

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

PLATFORM_CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
ifeq ($(UNAME), Darwin)
PLATFORM_CUDA_LDFLAGS=-L/usr/local/cuda/lib -lcuda
endif

PLATFORM_OPENGL_LDFLAGS=-lGL -lX11
ifeq ($(UNAME), Darwin)
PLATFORM_OPENGL_LDFLAGS=-framework OpenGL
endif

PLATFORM_OPENCL_LDFLAGS=-lOpenCL
ifeq ($(UNAME), Darwin)
PLATFORM_OPENCL_LDFLAGS=-F/Library/Frameworks -framework OpenCL
endif

ifneq (,$(findstring ptx,$(HL_TARGET)))
  CUDA_LDFLAGS=$(PLATFORM_CUDA_LDFLAGS)
endif
ifneq (,$(findstring cuda,$(HL_TARGET)))
  CUDA_LDFLAGS=$(PLATFORM_CUDA_LDFLAGS)
endif
ifneq (, $(findstring opencl,$(HL_TARGET)))
  OPENCL_LDFLAGS=$(PLATFORM_OPENCL_LDFLAGS)
endif
ifneq (, $(findstring opengl,$(HL_TARGET)))
  OPENGL_LDFLAGS=$(PLATFORM_OPENGL_LDFLAGS)
endif

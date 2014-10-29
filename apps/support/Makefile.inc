CXX ?= g++
GXX ?= g++

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

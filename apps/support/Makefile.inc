CXX ?= g++
GXX ?= g++

ifdef BUILD_PREFIX
LIB_HALIDE = bin/$(BUILD_PREFIX)/libHalide.a
else
LIB_HALIDE = bin/libHalide.a
endif

LDFLAGS += $(shell llvm-config --ldflags)

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

PLATFORM_CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
ifeq ($(UNAME), Darwin)
PLATFORM_CUDA_LDFLAGS=-F/Library/Frameworks -framework CUDA
endif

UNAME = $(shell uname)

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
ifneq (, $(findstring spir,$(HL_TARGET)))
  OPENCL_LDFLAGS=$(PLATFORM_OPENCL_LDFLAGS)
endif
ifneq (, $(findstring spir64,$(HL_TARGET)))
  OPENCL_LDFLAGS=$(PLATFORM_OPENCL_LDFLAGS)
endif

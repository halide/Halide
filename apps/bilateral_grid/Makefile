CXX ?= g++

ifdef BUILD_PREFIX
LIB_HALIDE = bin/$(BUILD_PREFIX)/libHalide.a
else
LIB_HALIDE = bin/libHalide.a
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


ifneq (,$(findstring ptx,$(HL_TARGET)))
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif
ifneq (,$(findstring cuda,$(HL_TARGET)))
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif
ifneq (, $(findstring opencl,$(HL_TARGET)))
  OPENCL_LDFLAGS=-lOpenCL
endif
ifneq (, $(findstring spir,$(HL_TARGET)))
  OPENCL_LDFLAGS=-lOpenCL
endif
ifneq (, $(findstring spir64,$(HL_TARGET)))
  OPENCL_LDFLAGS=-lOpenCL
endif

all: filter

$(LIB_HALIDE): ../.. ../../src
	$(MAKE) -C ../../ $(LIB_HALIDE)

bilateral_grid: bilateral_grid.cpp $(LIB_HALIDE)
	$(CXX) $(CPPFLAGS) bilateral_grid.cpp -I ../../include/  ../../$(LIB_HALIDE) -o bilateral_grid  -lpthread -ldl

bilateral_grid.o: bilateral_grid
	./bilateral_grid 8

filter: bilateral_grid.o filter.cpp
	$(CXX) -I../support -O3 -ffast-math -Wall -Werror filter.cpp bilateral_grid.o -lpthread -ldl -o filter  $(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

out.png: filter
	./filter ../images/gray.png out.png 0.1

clean:
	rm -f bilateral_grid bilateral_grid.o bilateral_grid.h filter

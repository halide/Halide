# We really need to use g++ here, to support openmp
GXX ?= g++

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

ifeq ($(HL_TARGET),ptx)
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif
ifeq ($(HL_TARGET),ptx-debug)
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif

halide_blur: ../../ halide_blur.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(GXX) halide_blur.cpp -I ../../include ../../$(LIB_HALIDE) -o halide_blur -ldl -lpthread

halide_blur.o: halide_blur
	./halide_blur

# g++ on OS X might actually be system clang without openmp
GXX_VERSION=$(shell $(GXX) --version)
ifeq (,$(findstring clang,$(GXX_VERSION)))
OPENMP_FLAGS=-fopenmp
else
OPENMP_FLAGS=
endif

# -O2 is faster than -O3 for this app (O3 unrolls too much)
test: test.cpp halide_blur.o
	$(GXX) $(OPENMP_FLAGS) -msse2 -Wall -O2 -I ../support/ test.cpp halide_blur.o -o test -lpthread -ldl $(PNGFLAGS) $(CUDA_LDFLAGS)

clean:
	rm -f test halide_blur.o halide_blur

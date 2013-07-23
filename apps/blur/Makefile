# We really need to use g++ here, to support openmp
GXX ?= g++


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

halide_blur: ../../ halide_blur.cpp
	$(MAKE) -C ../../ bin/libHalide.a
	$(GXX) halide_blur.cpp -I ../../include ../../bin/libHalide.a -o halide_blur -ldl -lpthread

halide_blur.o: halide_blur
	./halide_blur

# -O2 is faster than -O3 for this app (O3 unrolls too much)
test: test.cpp halide_blur.o
	$(GXX) -msse2 -fopenmp -lpng -Wall -O2 -I ../support/ test.cpp halide_blur.o -o test -lpthread -ldl -lpng $(PNGFLAGS) $(CUDA_LDFLAGS)

clean:
	rm -f test halide_blur.o halide_blur

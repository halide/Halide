GXX ?= g++

PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)

ifeq ($(HL_TARGET),ptx)
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif

halide_blur: ../../ halide_blur.cpp
	$(MAKE) -C ../../ bin/libHalide.a
	$(GXX) halide_blur.cpp -I ../../include ../../bin/libHalide.a -o halide_blur -ldl -lpthread

halide_blur.o: halide_blur
	HL_DISABLE_BOUNDS_CHECKING=1 ./halide_blur

# -O2 is faster than -O3 for this app (O3 unrolls too much)
test: test.cpp halide_blur.o
	$(GXX) -fopenmp -lpng -Wall -O2 test.cpp halide_blur.o -o test -lpthread -ldl -lpng $(PNGFLAGS) $(CUDA_LDFLAGS)

clean:
	rm -f test halide_blur.o halide_blur

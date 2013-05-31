GXX ?= g++

PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)

halide_blur: ../../ halide_blur.cpp
	$(MAKE) -C ../../ bin/libHalide.a
	$(GXX) halide_blur.cpp -I ../../include ../../bin/libHalide.a -o halide_blur -ldl -lpthread

halide_blur.o: halide_blur
	HL_DISABLE_BOUNDS_CHECKING=1 ./halide_blur

halide_blur_cvl.o: halide_blur
	HL_DISABLE_BOUNDS_CHECKING=1 ./halide_blur

# -O2 is faster than -O3 for this app (O3 unrolls too much)
test: test.cpp halide_blur.o
	$(GXX) -fopenmp -lpng -Wall -O2 test.cpp halide_blur.o halide_blur_cvl.o -o test -lpthread -ldl -lpng $(PNGFLAGS)

process: process.cpp halide_blur_cvl.o
	$(GXX) -I../support -Wall -O3 process.cpp halide_blur_cvl.o -o process -lpthread -ldl $(PNGFLAGS) $(CUDA_LFLAGS)

clean:
	rm -f test halide_blur.o halide_blur_cvl.o halide_blur test process

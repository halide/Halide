GXX = g++-4.6
ifneq ($(shell env -i command -v g++-4.7),)
GXX = g++-4.7
endif

LLVM_ROOT=../../llvm/Release+Asserts
LLC=$(LLVM_ROOT)/bin/llc
OPT=$(LLVM_ROOT)/bin/opt
PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)

halide_blur: ../../cpp_bindings/ ../../src/ halide_blur.cpp
	$(MAKE) -C ../../cpp_bindings Halide.a
	$(GXX) halide_blur.cpp -I ../../cpp_bindings/ ../../cpp_bindings/Halide.a  -std=c++0x -o halide_blur -ldl -lpthread

halide_blur.bc: halide_blur
	./halide_blur

halide_blur.o: halide_blur.bc
	cat halide_blur.bc | $(OPT) -O3 | $(LLC) -O3 -filetype=obj > halide_blur.o

test: test.cpp halide_blur.o
	$(GXX) -fopenmp -std=c++0x -lpng -Wall -O3 test.cpp halide_blur.o -o test -lpthread -ldl -lpng -L/usr/local/cuda/lib -lcuda  $(PNGFLAGS)

clean:
	rm -f test halide_blur.o halide_blur.bc halide_blur

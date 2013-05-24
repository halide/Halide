ifneq ($(shell env -i command -v g++-4.7),)
GXX ?= g++-4.7
endif
GXX ?= g++-4.6

LLVM_ROOT=../../llvm/Release+Asserts
LLC=$(LLVM_ROOT)/bin/llc
OPT=$(LLVM_ROOT)/bin/opt
PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)

halide_blur: ../../cpp_bindings/ ../../src/ halide_blur.cpp
	$(MAKE) -C ../../cpp_bindings libHalide.a
	$(GXX) halide_blur.cpp -I ../../cpp_bindings/ ../../cpp_bindings/libHalide.a  -std=c++0x -o halide_blur -ldl -lpthread

halide_blur_%.bc: halide_blur
	HL_DISABLE_BOUNDS_CHECKING=1 ./halide_blur

halide_blur_%.o: halide_blur_%.bc
	cat $< | $(OPT) -O3 -always-inline | $(LLC) -O3 -filetype=obj > $@

# -O2 is faster than -O3 for this app (O3 unrolls too much)
test: test.cpp halide_blur_sliding_scanlines.o #halide_blur_breadth_first.o
	$(GXX) -fopenmp -std=c++0x -lpng -Wall -O2 $^ -o test -lpthread -ldl -lpng $(PNGFLAGS)

clean:
	rm -f test halide_blur.o halide_blur.bc halide_blur

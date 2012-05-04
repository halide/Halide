LLVM_ROOT=../../llvm/Release+Asserts
LLC=$(LLVM_ROOT)/bin/llc
OPT=$(LLVM_ROOT)/bin/opt

halide_blur: ../../cpp_bindings/ ../../src/ halide_blur.cpp
	$(MAKE) -C ../../cpp_bindings Halide.a
	g++-4.6 halide_blur.cpp -I ../../cpp_bindings/ ../../cpp_bindings/Halide.a  -std=c++0x -o halide_blur -ldl -lpthread

halide_blur.bc: halide_blur
	./halide_blur

halide_blur.o: halide_blur.bc
	cat halide_blur.bc | opt -O3 | llc -O3 -filetype=obj > halide_blur.o

test: test.cpp halide_blur.o
	g++-4.6 -std=c++0x -lpng -Wall -O3 test.cpp halide_blur.o -o test -lpthread -ldl -lpng -L/usr/local/cuda/lib -lcuda

clean: 
	rm -f test halide_blur.o halide_blur.bc halide_blur

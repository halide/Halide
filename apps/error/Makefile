PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)

error: ../../cpp_bindings/ error.cpp
	$(MAKE) -C ../../cpp_bindings Halide.a
	g++-4.6 error.cpp -I ../../cpp_bindings/ ../../cpp_bindings/Halide.a  -std=c++0x -lpng -o error -lpthread -ldl $(PNGFLAGS)

clean: 
	rm error

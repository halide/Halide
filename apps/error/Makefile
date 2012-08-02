ifneq ($(shell env -i command -v g++-4.7),)
GXX ?= g++-4.7
endif
GXX ?= g++-4.6

PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)

error: ../../cpp_bindings/ error.cpp
	$(MAKE) -C ../../cpp_bindings libHalide.a
	$(GXX) error.cpp -I ../../cpp_bindings/ ../../cpp_bindings/libHalide.a  -std=c++0x -lpng -o error -lpthread -ldl $(PNGFLAGS)

clean: 
	rm error

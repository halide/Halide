PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)
ifeq ($(HL_TARGET),ptx)
  SCHEDULE=103
  CUDA_LFLAGS=-L/usr/local/cuda/lib -lcuda
else
  SCHEDULE=12
endif

all: lighten

lighten: ../../cpp_bindings/ ../png.h lighten.cpp
	$(MAKE) -C ../../cpp_bindings Halide.a
	g++-4.6 lighten.cpp -I ../../cpp_bindings/ ../../cpp_bindings/Halide.a  -std=c++0x -lpng -o lighten -lpthread -ldl $(PNGFLAGS)

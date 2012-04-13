PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)
ifeq ($(HL_TARGET),ptx)
  SCHEDULE=103
  CUDA_LFLAGS=-L/usr/local/cuda/lib -lcuda
else
  SCHEDULE=12
endif

all: patchmatch

patchmatch: ../../cpp_bindings/ ../png.h patchmatch.cpp
	$(MAKE) -C ../../cpp_bindings Halide.a
	g++-4.6 patchmatch.cpp -I ../../cpp_bindings/ ../../cpp_bindings/Halide.a  -std=c++0x -lpng -o patchmatch -lpthread -ldl $(PNGFLAGS)

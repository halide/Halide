ifneq ($(shell env -i command -v g++-4.7),)
GXX ?= g++-4.7
endif
GXX ?= g++-4.6

PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)
ifeq ($(HL_TARGET),ptx)
  SCHEDULE=103
  CUDA_LFLAGS=-L/usr/local/cuda/lib -lcuda
else
  SCHEDULE=12
endif

all: interpolate

interpolate: ../../cpp_bindings/ ../png.h interpolate.cpp
	$(MAKE) -C ../../cpp_bindings libHalide.a
	$(GXX) -g interpolate.cpp -I ../../cpp_bindings/ ../../cpp_bindings/libHalide.a  -std=c++0x -lpng -o interpolate -lpthread -ldl $(PNGFLAGS)

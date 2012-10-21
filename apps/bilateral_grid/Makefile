GXX ?= g++

PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)
ifeq ($(HL_TARGET),ptx)
  #SCHEDULE=103
  CUDA_LFLAGS=-L/usr/local/cuda/lib -lcuda
  CPPFLAGS=-DUSE_GPU=1
else
  #SCHEDULE=12
endif

all: filter

bilateral_grid: ../../cpp_bindings/ bilateral_grid.cpp
	$(MAKE) -C ../../cpp_bindings libHalide.a
	$(GXX) $(CPPFLAGS) bilateral_grid.cpp -I ../../cpp_bindings/ ../../cpp_bindings/libHalide.a  -o bilateral_grid  -lpthread -ldl

bilateral_grid.bc: bilateral_grid bilateral_grid.cpp
	./bilateral_grid 8

bilateral_grid.o: bilateral_grid.bc
	../../llvm/Release+Asserts/bin/opt -O3 bilateral_grid.bc | ../../llvm/Release+Asserts/bin/llc -O3 -filetype=obj -o bilateral_grid.o

filter: bilateral_grid.o filter.cpp
	$(GXX) -I../../support -O3 -ffast-math -Wall -Werror filter.cpp bilateral_grid.o -lpng -lpthread -ldl -o filter  $(PNGFLAGS) $(CUDA_LFLAGS)

out.png: filter
	./filter input.png out.png 0.1

clean:
	rm bilateral_grid bilateral_grid.o bilateral_grid.h bilateral_grid.bc filter

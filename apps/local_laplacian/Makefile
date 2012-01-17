PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)
ifeq ($(HL_TARGET),ptx)
  SCHEDULE=103
  CUDA_LFLAGS=-L/usr/local/cuda/lib -lcuda
else
  SCHEDULE=12
endif

all: process

local_laplacian: ../../cpp_bindings/ local_laplacian.cpp
	$(MAKE) -C ../../cpp_bindings Halide.a
	g++-4.6 local_laplacian.cpp -I ../../cpp_bindings/ ../../cpp_bindings/Halide.a  -std=c++0x -lpng -o local_laplacian -lpthread -ldl $(PNGFLAGS)

local_laplacian.bc: local_laplacian
	./local_laplacian $(SCHEDULE)

local_laplacian.o: local_laplacian.bc
	cat local_laplacian.bc | opt -O3 | llc -O3 -filetype=obj -o local_laplacian.o

process: process.cpp local_laplacian.o ../Util.h ../png.h
	g++-4.6 -std=c++0x -Wall -O3 process.cpp local_laplacian.o -o process -lpthread -ldl $(PNGFLAGS) $(CUDA_LFLAGS)

out.png: process
	./process input.png 8 1 1 out.png

clean: 
	rm process local_laplacian.o local_laplacian.bc local_laplacian

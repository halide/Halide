GXX ?= g++

PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)
ifeq ($(HL_TARGET),ptx)
  SCHEDULE=103
  CUDA_LFLAGS=-L/usr/local/cuda/lib -lcuda
else
  SCHEDULE=12
endif

all: process

local_laplacian: ../../ local_laplacian_16_2.cpp
	$(MAKE) -C ../../ bin/libHalide.a
	$(GXX) local_laplacian_16_2.cpp -I ../../include ../../bin/libHalide.a -o local_laplacian -lpthread -ldl -O3

local_laplacian.o: local_laplacian
	./local_laplacian

process: process.cpp local_laplacian.o
	$(GXX) -I../support -Wall -O3 process.cpp local_laplacian.o -o process -lpthread -ldl $(PNGFLAGS) $(CUDA_LFLAGS)

out.png: process
	./process input.png 1 1 out.png

clean:
	rm -f process local_laplacian.o local_laplacian

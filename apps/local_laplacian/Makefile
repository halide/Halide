GXX ?= g++

PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)

ifeq ($(HL_TARGET),ptx)
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif

all: process

local_laplacian: ../../ local_laplacian.cpp
	$(MAKE) -C ../../ bin/libHalide.a
	$(GXX) local_laplacian.cpp -I ../../include ../../bin/libHalide.a -o local_laplacian -lpthread -ldl -O3

local_laplacian.o: local_laplacian
	./local_laplacian

process: process.cpp local_laplacian.o
	$(GXX) -I../support -Wall -O3 process.cpp local_laplacian.o -o process -lpthread -ldl $(PNGFLAGS) $(CUDA_LDFLAGS)

out.png: process
	./process input.png 8 1 1 out.png

clean:
	rm -f process local_laplacian.o local_laplacian

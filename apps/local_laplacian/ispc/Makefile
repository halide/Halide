PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)

all: process

local_laplacian.o: local_laplacian.ispc
	ispc $< -o $@

process: process.cpp local_laplacian.o ../../Util.h ../../png.h
	g++-4.6 -fopenmp -std=c++0x -Wall -O3 process.cpp local_laplacian.o -o process -lpthread -ldl $(PNGFLAGS)

out.png: process
	./process input.png 8 1 1 out.png

clean: 
	rm -f process local_laplacian.o

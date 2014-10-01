include ../support/Makefile.inc

all: process

local_laplacian: ../../ local_laplacian.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) local_laplacian.cpp -g -I ../../include ../../$(LIB_HALIDE) -o local_laplacian -lpthread -ldl -lz $(LDFLAGS)

local_laplacian.o: local_laplacian
	./local_laplacian

process: process.cpp local_laplacian.o
	$(CXX) -I../support -Wall -Werror -Wno-unused-function -O3 process.cpp local_laplacian.o -o process -lpthread -ldl $(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

out.png: process
	./process ../images/rgb.png 8 1 1 out.png

clean:
	rm -f process local_laplacian.o local_laplacian

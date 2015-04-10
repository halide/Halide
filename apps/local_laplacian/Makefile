include ../support/Makefile.inc

all: process

local_laplacian_gen: ../../ local_laplacian_gen.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) $(CXXFLAGS) local_laplacian_gen.cpp -g ../../$(LIB_HALIDE) -o local_laplacian_gen -lpthread -ldl -lz \
	$(LDFLAGS) $(LLVM_SHARED_LIBS)

local_laplacian.o: local_laplacian_gen
	./local_laplacian_gen

process: process.cpp local_laplacian.o
	$(CXX) $(CXXFLAGS) -Wall -O3 process.cpp local_laplacian.o -o process -lpthread -ldl $(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

out.png: process
	./process ../images/rgb.png 8 1 1 out.png

clean:
	rm -f process local_laplacian.o local_laplacian_gen

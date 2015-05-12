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
	./process ../images/rgb.png 8 1 1 10 out.png

# Build rules for generating a visualization of the pipeline using HalideTraceViz
process_viz: local_laplacian_viz.o
	$(CXX) $(CXXFLAGS) -Wall -O3 process.cpp local_laplacian_viz.o -o process_viz -lpthread -ldl $(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

local_laplacian_viz.o: local_laplacian_gen
	HL_TRACE=3 ./local_laplacian_gen 6
	mv local_laplacian.o local_laplacian_viz.o

local_laplacian.avi: ../../bin/HalideTraceViz process_viz
	bash viz.sh

clean:
	rm -f process local_laplacian.o process_viz local_laplacian_viz.o local_laplacian_gen local_laplacian.avi

include ../support/Makefile.inc

all: process

local_laplacian_gen: local_laplacian_gen.cpp
	$(CXX) $(CXXFLAGS) local_laplacian_gen.cpp -g $(LIB_HALIDE) -o local_laplacian_gen \
	$(LDFLAGS) $(LLVM_SHARED_LIBS)

local_laplacian.a: local_laplacian_gen
	./local_laplacian_gen

process: process.cpp local_laplacian.a
	$(CXX) $(CXXFLAGS) -Wall -O3 process.cpp local_laplacian.a -o process $(LDFLAGS) $(PNGFLAGS) $(OPENGL_LDFLAGS)

out.png: process
	./process ../images/rgb.png 8 1 1 10 out.png

# Build rules for generating a visualization of the pipeline using HalideTraceViz
process_viz: local_laplacian_viz.a
	$(CXX) $(CXXFLAGS) -Wall -O3 process.cpp local_laplacian_viz.a -o process_viz $(LDFLAGS) $(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

local_laplacian_viz.a: local_laplacian_gen
	HL_TRACE=3 ./local_laplacian_gen 6
	mv local_laplacian.a local_laplacian_viz.a

local_laplacian.mp4: process_viz
	bash viz.sh

clean:
	rm -f process local_laplacian.a process_viz local_laplacian_viz.a local_laplacian_gen local_laplacian.mp4

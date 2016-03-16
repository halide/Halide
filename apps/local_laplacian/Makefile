include ../support/Makefile.inc

all: process

SCHEDULE=0

local_laplacian_gen: local_laplacian_gen.cpp
	$(CXX) -DSCHEDULE=$(SCHEDULE) $(CXXFLAGS) local_laplacian_gen.cpp -g $(LIB_HALIDE) -o local_laplacian_gen \
	$(LDFLAGS) $(LLVM_SHARED_LIBS)

local_laplacian.o: local_laplacian_gen
	./local_laplacian_gen

ifdef HEX_TOOLS
process: process.cpp local_laplacian.o
	$(TGT_CXX) $(TGT_CXXFLAGS) -Wall -O3 process.cpp local_laplacian.o -o process -lhexagon
else
process: process.cpp local_laplacian.o
	$(CXX) $(CXXFLAGS) -Wall -O3 process.cpp local_laplacian.o -o process $(LDFLAGS) $(PNGFLAGS) $(OPENGL_LDFLAGS)
endif

out.png: process
	./process ../images/rgb.png 8 1 1 10 out.png

ifdef HEX_TOOLS
out.ppm: process
	${HEX_TOOLS}/bin/hexagon-sim ./process ${TIMING} -- ../images/rgb.ppm 8 1 1 1 out.ppm
	cmp out.ppm ref/out.ppm

out_%.ppm: process
	${HEX_TOOLS}/bin/hexagon-sim ./process ${TIMING} -- ../images/rgb_$*.ppm 8 1 1 1 out_$*.ppm
	cmp out_$*.ppm ref_$*/out_$*.ppm
endif

# Build rules for generating a visualization of the pipeline using HalideTraceViz
ifdef HEX_TOOLS
process_viz: local_laplacian_viz.o
	$(CXX) $(CXXFLAGS) -Wall -O3 process.cpp local_laplacian_viz.o -o process_viz -lhexagon
else
process_viz: local_laplacian_viz.o
	$(CXX) $(CXXFLAGS) -Wall -O3 process.cpp local_laplacian_viz.o -o process_viz $(LDFLAGS) $(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)
endif

local_laplacian_viz.o: local_laplacian_gen
	HL_TRACE=3 ./local_laplacian_gen 6
	mv local_laplacian.o local_laplacian_viz.o

local_laplacian.mp4: process_viz
	bash viz.sh

clean:
	rm -f process local_laplacian.o process_viz local_laplacian_viz.o local_laplacian_gen local_laplacian.mp4 \
	      out.png out_small.png out.ppm out_small.ppm pmu_statsfile.txt pmu_stats.txt

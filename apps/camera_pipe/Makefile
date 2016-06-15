include ../support/Makefile.inc

all: process

TIMING_ITERATIONS ?= 5

camera_pipe: ../../ camera_pipe.cpp
	$(CXX) $(CXXFLAGS) camera_pipe.cpp -g $(LIB_HALIDE) -o camera_pipe $(LDFLAGS)

curved.a: camera_pipe
	./camera_pipe 8 # 8-bit output,

fcam/Demosaic.o: fcam/Demosaic.cpp fcam/Demosaic.h
	$(CXX) $(CXXFLAGS) -c -Wall -O3 $< -o $@

fcam/Demosaic_ARM.o: fcam/Demosaic_ARM.cpp fcam/Demosaic_ARM.h
	$(CXX) $(CXXFLAGS) -c -Wall -O3 $< -o $@

process: process.cpp curved.a fcam/Demosaic.o fcam/Demosaic_ARM.o
	$(CXX) $(CXXFLAGS) -Wall -O3 $^ -o $@ $(PNGFLAGS) -ldl -lpthread

out.png: process
	./process ../images/bayer_raw.png 3700 2.0 50 $(TIMING_ITERATIONS) out.png

../../bin/HalideTraceViz:
	$(MAKE) -C ../../ bin/HalideTraceViz

camera_pipe.avi: camera_pipe.cpp viz.sh $(HALIDE_TRACE_VIZ) ../../bin/HalideTraceViz
	bash viz.sh

clean:
	rm -f out.png process curved.a camera_pipe fcam/*.o

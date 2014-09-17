include ../support/Makefile.inc

ifeq ($(HL_TARGET),ptx)
  SCHEDULE=100
else
  SCHEDULE=0
endif

all: process

camera_pipe: ../../ camera_pipe.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) camera_pipe.cpp -I ../../include/ -g ../../$(LIB_HALIDE) -o camera_pipe -ldl -lpthread -lz $(LDFLAGS)

curved.o: camera_pipe
	./camera_pipe 8 0 # 8-bit output,

fcam/Demosaic.o: fcam/Demosaic.cpp fcam/Demosaic.h
	$(CXX) -c -I../support -Wall -fopenmp -O3 $< -o $@

fcam/Demosaic_ARM.o: fcam/Demosaic_ARM.cpp fcam/Demosaic_ARM.h
	$(CXX) -c -I../support -Wall -fopenmp -O3 $< -o $@

process: process.cpp curved.o fcam/Demosaic.o fcam/Demosaic_ARM.o
	$(CXX) -I../support -Wall -O3 $^ -o $@ -lpthread -ldl -fopenmp $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS) $(PNGFLAGS)

out.png: process
	./process ../images/bayer_raw.png 3700 2.0 50 out.png

clean:
	rm -f out.png process curved.o camera_pipe fcam/*.o

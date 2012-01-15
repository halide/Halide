PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)
ifeq ($(HL_TARGET),ptx)
  SCHEDULE=100
  CUDA_LFLAGS=-L/usr/local/cuda/lib -lcuda
else
  SCHEDULE=0
endif

camera_pipe: ../../cpp_bindings/ ../../src/ camera_pipe.cpp
	$(MAKE) -C ../../cpp_bindings FImage.a
	g++-4.6 camera_pipe.cpp -I ../../cpp_bindings/ ../../cpp_bindings/FImage.a  -std=c++0x -o camera_pipe -ldl -lpthread

curved.bc: camera_pipe
	./camera_pipe 8 0 # 8-bit output, 

curved.o: curved.bc
	cat curved.bc | opt -O3 | llc -O3 -filetype=obj > curved.o

process: process.cpp curved.o
	g++-4.6 -std=c++0x -Wall -O3 process.cpp curved.o -o process -lpthread -ldl $(CUDA_LFLAGS) $(PNGFLAGS)

out.png: process raw.png
	./process raw.png 3700 2.0 50 out.png

clean: 
	rm -f out.png process curved.o curved.bc camera_pipe

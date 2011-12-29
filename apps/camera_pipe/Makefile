camera_pipe: ../../cpp_bindings/ camera_pipe.cpp
	$(MAKE) -C ../../cpp_bindings FImage.a
	g++-4.6 camera_pipe.cpp -I ../../cpp_bindings/ ../../cpp_bindings/FImage.a  -std=c++0x -o camera_pipe

curved.bc: camera_pipe
	./camera_pipe

curved.o: curved.bc
	llc curved.bc -O3 -filetype=obj

process: process.cpp curved.o
	g++-4.6 -std=c++0x -lpng -Wall -O3 process.cpp curved.o -o process


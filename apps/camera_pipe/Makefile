camera_pipe: ../../cpp_bindings/ camera_pipe.cpp
	$(MAKE) -C ../../cpp_bindings FImage.a
	g++-4.6 camera_pipe.cpp -I ../../cpp_bindings/ ../../cpp_bindings/FImage.a  -std=c++0x -o camera_pipe -ldl -lpthread

curved.bc: camera_pipe
	./camera_pipe

curved.o: curved.bc
	cat curved.bc | opt -O3 | llc -O3 -filetype=obj > curved.o

process: process.cpp curved.o
	g++-4.6 -std=c++0x -lpng -Wall -O3 process.cpp curved.o -o process -lpthread -ldl -lpng -L/usr/local/cuda/lib -lcuda

out.png: process raw.png
	./process raw.png 3700 2.0 50 out.png

clean: 
	rm -f out.png process curved.o curved.bc camera_pipe

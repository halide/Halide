all: process

local_laplacian: ../../cpp_bindings/ local_laplacian.cpp
	$(MAKE) -C ../../cpp_bindings FImage.a
	g++-4.6 local_laplacian.cpp -I ../../cpp_bindings/ ../../cpp_bindings/FImage.a  -std=c++0x -o local_laplacian

local_laplacian.bc: local_laplacian
	./local_laplacian 5 2

local_laplacian.o: local_laplacian.bc
	llc local_laplacian.bc -O3 -filetype=obj

process: process.cpp local_laplacian.o
	g++-4.6 -std=c++0x -lpng -Wall -O3 process.cpp local_laplacian.o -o process

clean: 
	rm process local_laplacian.o local_laplacian.bc local_laplacian

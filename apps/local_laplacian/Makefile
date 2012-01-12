all: process

local_laplacian: ../../cpp_bindings/ local_laplacian.cpp
	$(MAKE) -C ../../cpp_bindings FImage.a
	g++-4.6 local_laplacian.cpp -I ../../cpp_bindings/ ../../cpp_bindings/FImage.a  -std=c++0x -lpng -o local_laplacian -lpthread -ldl

local_laplacian.bc: local_laplacian
	./local_laplacian 12

local_laplacian.o: local_laplacian.bc
	cat local_laplacian.bc | opt -O3 | llc -O3 -filetype=obj -o local_laplacian.o

process: process.cpp local_laplacian.o
	g++-4.6 -std=c++0x -Wall -O3 process.cpp local_laplacian.o -o process -lpthread -ldl -lpng

clean: 
	rm process local_laplacian.o local_laplacian.bc local_laplacian

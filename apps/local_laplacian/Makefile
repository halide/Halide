ifneq ($(shell env -i command -v g++-4.7),)
GXX ?= g++-4.7
endif
GXX ?= g++-4.6

LLVM_ROOT=../../llvm/Release+Asserts
LLC=$(LLVM_ROOT)/bin/llc
OPT=$(LLVM_ROOT)/bin/opt

PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)
ifeq ($(HL_TARGET),ptx)
  SCHEDULE=103
  CUDA_LFLAGS=-L/usr/local/cuda/lib -lcuda
else
  SCHEDULE=12
endif

all: process

local_laplacian: ../../cpp_bindings/ local_laplacian.cpp
	$(MAKE) -C ../../cpp_bindings libHalide.a
	$(GXX) local_laplacian.cpp -I ../../cpp_bindings/ ../../cpp_bindings/libHalide.a  -std=c++0x -lpng -o local_laplacian -lpthread -ldl $(PNGFLAGS) -g

local_laplacian.bc: local_laplacian
	./local_laplacian $(SCHEDULE)

local_laplacian.o: local_laplacian.bc
	cat local_laplacian.bc | $(OPT) -O3 | $(LLC) -O3 -filetype=obj -o local_laplacian.o

process: process.cpp local_laplacian.o ../Util.h ../png.h
	$(GXX) -std=c++0x -Wall -O3 process.cpp local_laplacian.o -o process -lpthread -ldl $(PNGFLAGS) $(CUDA_LFLAGS)

out.png: process
	./process input.png 8 1 1 out.png

clean:
	rm process local_laplacian.o local_laplacian.bc local_laplacian

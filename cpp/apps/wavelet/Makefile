GXX ?= g++

HL_MODULES = daubechies_x.o inverse_daubechies_x.o haar_x.o inverse_haar_x.o

PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)
ifeq ($(HL_TARGET),ptx)
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif

all: filter

wavelet: ../../ wavelet.cpp
	$(MAKE) -C ../../ bin/libHalide.a
	$(GXX) wavelet.cpp -I ../../include ../../bin/libHalide.a -o wavelet -lpthread -ldl 

$(HL_MODULES): wavelet 
	./wavelet

filter.o: filter.cpp $(HL_MODULES)
	$(GXX) $(PNGFLAGS) -I../support -O3 -c filter.cpp

filter: filter.o
	$(GXX) filter.o $(HL_MODULES) $(PNGFLAGS) $(CUDA_LDFLAGS) -o filter -lpthread

clean:
	rm -f wavelet filter filter.o $(HL_MODULES) $(HL_MODULES:%.o) $(HL_MODULES:%.o=%.h)
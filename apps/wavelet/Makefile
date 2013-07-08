CXX ?= g++

HL_MODULES = daubechies_x.o inverse_daubechies_x.o haar_x.o inverse_haar_x.o

PNGFLAGS=-lpng $(shell libpng-config --cflags)
ifeq ($(HL_TARGET),ptx)
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif

all: filter

wavelet: ../../ wavelet.cpp
	$(MAKE) -C ../../ bin/libHalide.a
	$(CXX) wavelet.cpp -I ../../include ../../bin/libHalide.a -o wavelet -lpthread -ldl

$(HL_MODULES): wavelet
	./wavelet

filter.o: filter.cpp $(HL_MODULES)
	$(CXX) $(PNGFLAGS) -I../support -O3 -c filter.cpp

filter: filter.o
	$(CXX) filter.o $(HL_MODULES) $(PNGFLAGS) $(CUDA_LDFLAGS) -o filter -lpthread

clean:
	rm -f wavelet filter filter.o $(HL_MODULES) $(HL_MODULES:%.o) $(HL_MODULES:%.o=%.h)

test: filter
	./filter ../images/gray.png
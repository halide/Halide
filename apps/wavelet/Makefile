include ../support/Makefile.inc

HL_MODULES = daubechies_x.o inverse_daubechies_x.o haar_x.o inverse_haar_x.o

all: filter

wavelet: ../../ wavelet.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) $(CXXFLAGS) wavelet.cpp -g ../../$(LIB_HALIDE) -o wavelet -lpthread -ldl -lz $(LDFLAGS)

$(HL_MODULES): wavelet
	./wavelet

filter.o: filter.cpp $(HL_MODULES)
	$(CXX) $(CXXFLAGS) $(LIBPNG_CXX_FLAGS) -O3 -c filter.cpp

filter: filter.o
	$(CXX) $(CXXFLAGS) filter.o $(HL_MODULES) $(PNGFLAGS) -o filter -lpthread -ldl

clean:
	rm -f wavelet filter filter.o $(HL_MODULES) $(HL_MODULES:%.o) $(HL_MODULES:%.o=%.h)

test: filter
	./filter ../images/gray.png

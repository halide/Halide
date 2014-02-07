include ../support/Makefile.inc

CXXFLAGS += -g -Wall

.PHONY: all clean

all: interpolate resample

interpolate: ../../ interpolate.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) $(CXXFLAGS) interpolate.cpp -I ../../include/ -I ../support ../../$(LIB_HALIDE) -o interpolate  -lpthread -ldl $(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

resample: ../../ resample.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) $(CXXFLAGS) resample.cpp -I ../../include/ -I ../support ../../$(LIB_HALIDE) -o resample  -lpthread -ldl $(PNGFLAGS)

out.png: interpolate
	./interpolate ../images/rgba.png out.png

clean:
	rm -f interpolate interpolate.h out.png resample

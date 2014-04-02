include ../support/Makefile.inc

CXXFLAGS += -g -Wall

.PHONY: clean

interpolate: ../../ interpolate.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) $(CXXFLAGS) interpolate.cpp -I ../../include/ -I ../support $(LDFLAGS) ../../$(LIB_HALIDE) -o interpolate  -lpthread -ldl -lz $(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

out.png: interpolate
	./interpolate ../images/rgba.png out.png

clean:
	rm -f interpolate interpolate.h out.png

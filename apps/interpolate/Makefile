include ../support/Makefile.inc

CXXFLAGS += -g -Wall -Werror -Wno-unused-function 

.PHONY: clean

interpolate: ../../ interpolate.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) $(CXXFLAGS) interpolate.cpp -I ../../include/ -I ../support ../../$(LIB_HALIDE) -o interpolate -lpthread -ldl -lz $(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS) $(LDFLAGS)

out.png: interpolate
	./interpolate ../images/rgba.png out.png

clean:
	rm -f interpolate interpolate.h out.png

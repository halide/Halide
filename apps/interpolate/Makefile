include ../support/Makefile.inc

all: interpolate

interpolate: ../../ interpolate.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) $(CPPFLAGS) interpolate.cpp -I ../../include/ -I ../support ../../$(LIB_HALIDE) -o interpolate  -lpthread -ldl $(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

out.png: interpolate
	./interpolate ../images/rgba.png out.png

clean:
	rm -f interpolate interpolate.h out.png

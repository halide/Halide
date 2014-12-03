include ../support/Makefile.inc

CXXFLAGS += -g -Wall

.PHONY: clean

interpolate: ../../ interpolate.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) $(CXXFLAGS) interpolate.cpp ../../$(LIB_HALIDE) -o interpolate -lpthread -ldl -lz \
	$(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS) $(LDFLAGS) $(LLVM_SHARED_LIBS)

out.png: interpolate
	./interpolate ../images/rgba.png out.png

clean:
	rm -f interpolate interpolate.h out.png

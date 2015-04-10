include ../support/Makefile.inc

all: test

halide_blur: ../../ halide_blur.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) $(CXXFLAGS) halide_blur.cpp ../../$(LIB_HALIDE) -o halide_blur -ldl -lpthread -lz $(LDFLAGS)

halide_blur.o: halide_blur
	./halide_blur

# g++ on OS X might actually be system clang without openmp
CXX_VERSION=$(shell $(CXX) --version)
ifeq (,$(findstring clang,$(CXX_VERSION)))
OPENMP_FLAGS=-fopenmp
else
OPENMP_FLAGS=
endif

# -O2 is faster than -O3 for this app (O3 unrolls too much)
test: test.cpp halide_blur.o
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -msse2 -Wall -O2 test.cpp halide_blur.o -o test -lpthread -ldl $(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -f test halide_blur.o halide_blur

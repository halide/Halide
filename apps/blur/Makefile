include ../support/Makefile.inc

all: test

halide_blur: halide_blur.cpp
	$(CXX) $(CXXFLAGS) halide_blur.cpp $(LIB_HALIDE) -o halide_blur $(LDFLAGS)

halide_blur.a: halide_blur
	./halide_blur

# g++ on OS X might actually be system clang without openmp
CXX_VERSION=$(shell $(CXX) --version)
ifeq (,$(findstring clang,$(CXX_VERSION)))
OPENMP_FLAGS=-fopenmp
else
OPENMP_FLAGS=
endif

# -O2 is faster than -O3 for this app (O3 unrolls too much)
test: test.cpp halide_blur.a
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -msse2 -Wall -O2 test.cpp halide_blur.a -o test $(LDFLAGS) $(PNGFLAGS)

clean:
	rm -f test halide_blur.a halide_blur

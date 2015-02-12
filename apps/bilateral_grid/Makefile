include ../support/Makefile.inc

all: filter

$(LIB_HALIDE): ../.. ../../src
	$(MAKE) -C ../../ $(LIB_HALIDE)

bilateral_grid: bilateral_grid.cpp $(LIB_HALIDE)
	$(CXX) $(CXXFLAGS) bilateral_grid.cpp -g ../../$(LIB_HALIDE) -o bilateral_grid -lpthread -ldl -lz $(LDFLAGS) \
	$(LLVM_SHARED_LIBS)

bilateral_grid.o: bilateral_grid
	./bilateral_grid 8

filter: bilateral_grid.o filter.cpp
	$(CXX) $(CXXFLAGS) -O3 -ffast-math -Wall -Werror filter.cpp bilateral_grid.o -lpthread -ldl -o filter  $(PNGFLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

out.png: filter
	./filter ../images/gray.png out.png 0.1

clean:
	rm -f bilateral_grid bilateral_grid.o bilateral_grid.h filter

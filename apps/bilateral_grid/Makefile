include ../support/Makefile.inc

all: filter

bilateral_grid: bilateral_grid.cpp
	$(CXX) $(CXXFLAGS) bilateral_grid.cpp -g $(LIB_HALIDE) -o bilateral_grid -lz $(LDFLAGS) \
	$(LLVM_SHARED_LIBS)

bilateral_grid.a: bilateral_grid
	./bilateral_grid 8

filter: bilateral_grid.a filter.cpp
	$(CXX) $(CXXFLAGS) -O3 -ffast-math -Wall -Werror filter.cpp bilateral_grid.a -o filter  $(PNGFLAGS) $(LDFLAGS)

bilateral_grid.mp4: bilateral_grid.cpp viz.sh
	bash viz.sh

out.png: filter
	./filter ../images/gray.png out.png 0.1 10

clean:
	rm -f bilateral_grid bilateral_grid.mp4 bilateral_grid.a bilateral_grid.h bilateral filter

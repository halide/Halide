all: filter

bilateral_grid: ../../cpp_bindings/ bilateral_grid.cpp
	$(MAKE) -C ../../cpp_bindings FImage.a
	g++-4.6 bilateral_grid.cpp -I ../../cpp_bindings/ ../../cpp_bindings/FImage.a  -std=c++0x -o bilateral_grid

bilateral_grid.bc: bilateral_grid bilateral_grid.cpp
	./bilateral_grid 16

bilateral_grid.o: bilateral_grid.bc
	opt -O3 bilateral_grid.bc | llc -O3 -filetype=obj -o bilateral_grid.o

filter: bilateral_grid.o filter.cpp 
	g++-4.6 -O3 -ffast-math -Wall -Werror -std=c++0x filter.cpp bilateral_grid.o -lpng -o filter

clean:
	rm bilateral_grid bilateral_grid.o bilateral_grid.h bilateral_grid.bc filter
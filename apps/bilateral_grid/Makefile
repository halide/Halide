all: filter

bilateral_grid: ../../cpp_bindings/ bilateral_grid.cpp
	$(MAKE) -C ../../cpp_bindings FImage.a
	g++-4.6 bilateral_grid.cpp -I ../../cpp_bindings/ ../../cpp_bindings/FImage.a  -std=c++0x -o bilateral_grid

clarity.bc: bilateral_grid
	./bilateral_grid

clarity.o: clarity.bc
	llc -O3 clarity.bc -filetype=obj

filter: clarity.o filter.cpp 
	g++-4.6 -Wall -Werror -std=c++0x filter.cpp clarity.o -lpng -o filter

clean:
	rm bilateral_grid clarity.o clarity.h clarity.bc filter
GXX ?= g++

PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)

all: filter

bilateral_grid: ../../ bilateral_grid.cpp
	$(MAKE) -C ../../ bin/libHalide.a
	$(GXX) $(CPPFLAGS) bilateral_grid.cpp -I ../../src/ ../../bin/libHalide.a -o bilateral_grid  -lpthread -ldl

bilateral_grid.o: bilateral_grid
	./bilateral_grid 8

filter: bilateral_grid.o filter.cpp
	$(GXX) -I../support -O3 -ffast-math -Wall -Werror filter.cpp bilateral_grid.o -lpng -lpthread -ldl -o filter  $(PNGFLAGS)

out.png: filter
	./filter input.png out.png 0.1

clean:
	rm -f bilateral_grid bilateral_grid.o bilateral_grid.h filter

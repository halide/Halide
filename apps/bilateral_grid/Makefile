CXX ?= g++

LIBPNG_LIBS_DEFAULT = $(shell libpng-config --ldflags)
LIBPNG_CXX_FLAGS ?= $(shell libpng-config --cflags)
# Workaround for libpng-config pointing to 64-bit versions on linux even when we're building for 32-bit
ifneq (,$(findstring -m32,$(CXX)))
ifneq (,$(findstring x86_64,$(LIBPNG_LIBS_DEFAULT)))
LIBPNG_LIBS ?= -lpng
endif
endif
LIBPNG_LIBS ?= $(LIBPNG_LIBS_DEFAULT)
PNGFLAGS=$(LIBPNG_LIBS) $(LIBPNG_CXX_FLAGS)

ifeq ($(HL_TARGET),ptx)
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif
ifeq ($(HL_TARGET),ptx-debug)
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif

all: filter

bilateral_grid: ../../ bilateral_grid.cpp
	$(MAKE) -C ../../ bin/libHalide.a
	$(CXX) $(CPPFLAGS) bilateral_grid.cpp -I ../../include/ ../../bin/libHalide.a -o bilateral_grid  -lpthread -ldl

bilateral_grid.o: bilateral_grid
	./bilateral_grid 8

filter: bilateral_grid.o filter.cpp
	$(CXX) -I../support -O3 -ffast-math -Wall -Werror filter.cpp bilateral_grid.o -lpthread -ldl -o filter  $(PNGFLAGS) $(CUDA_LDFLAGS)

out.png: filter
	./filter ../images/gray.png out.png 0.1

clean:
	rm -f bilateral_grid bilateral_grid.o bilateral_grid.h filter

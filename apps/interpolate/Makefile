GXX ?= g++

PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)

ifeq ($(HL_TARGET),ptx)
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif

all: interpolate

interpolate: ../../ interpolate.cpp
	$(MAKE) -C ../../ bin/libHalide.a
	$(GXX) $(CPPFLAGS) interpolate.cpp -I ../../include/ -I ../support ../../bin/libHalide.a -o interpolate  -lpthread -ldl $(PNGFLAGS) $(CUDA_LDFLAGS)

out.png: interpolate
	./interpolate in.png out.png

clean:
	rm -f interpolate interpolate.h out.png

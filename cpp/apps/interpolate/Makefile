GXX ?= g++

PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)

all: interpolate

interpolate: ../../ interpolate.cpp
	$(MAKE) -C ../../ bin/libHalide.a
	$(GXX) $(CPPFLAGS) interpolate.cpp -I ../../include/ -I ../support ../../bin/libHalide.a -o interpolate  -lpthread -ldl $(PNGFLAGS)

out.png: interpolate
	./interpolate in.png out.png

clean:
	rm -f interpolate interpolate.h out.png

CXX ?= g++

PNGFLAGS=-L/opt/local/lib -lpng $(shell libpng-config --cflags)

ifeq ($(HL_TARGET),ptx)
  CUDA_LDFLAGS=-L/usr/lib/nvidia-current -lcuda
endif

all: interpolate

interpolate: ../../ interpolate.cpp
	$(MAKE) -C ../../ bin/libHalide.a
	$(CXX) $(CPPFLAGS) interpolate.cpp -I ../../include/ -I ../support ../../bin/libHalide.a -o interpolate  -lpthread -ldl $(PNGFLAGS) $(CUDA_LDFLAGS)

out.png: interpolate
	./interpolate ../images/rgba.png out.png

clean:
	rm -f interpolate interpolate.h out.png

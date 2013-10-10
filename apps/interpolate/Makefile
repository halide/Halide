CXX ?= g++

ifdef BUILD_PREFIX
LIB_HALIDE = bin/$(BUILD_PREFIX)/libHalide.a
else
LIB_HALIDE = bin/libHalide.a
endif

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

all: interpolate

interpolate: ../../ interpolate.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) $(CPPFLAGS) interpolate.cpp -I ../../include/ -I ../support ../../$(LIB_HALIDE) -o interpolate  -lpthread -ldl $(PNGFLAGS) $(CUDA_LDFLAGS)

out.png: interpolate
	./interpolate ../images/rgba.png out.png

clean:
	rm -f interpolate interpolate.h out.png

ifneq ($(shell env -i command -v g++-4.7),)
GXX ?= g++-4.7
endif
GXX ?= g++-4.6

PNGFLAGS=$(shell libpng-config --ldflags) $(shell libpng-config --cflags)
ifeq ($(HL_TARGET),ptx)
  SCHEDULE=103
  CUDA_LFLAGS=-L/usr/local/cuda/lib -lcuda
else
  SCHEDULE=12
endif

all: process

seam_carving: ../../cpp_bindings/ seam_carving.cpp
	$(MAKE) -C ../../cpp_bindings libHalide.a
	$(GXX) seam_carving.cpp -I ../../cpp_bindings/ ../../cpp_bindings/libHalide.a  -std=c++0x -lpng -o seam_carving -lpthread -ldl $(PNGFLAGS)

seam_carving.bc: seam_carving
	./seam_carving $(SCHEDULE)

seam_carving.o: seam_carving.bc
	cat seam_carving.bc | opt -O3 | llc -O3 -filetype=obj -o seam_carving.o

process: process.cpp seam_carving.o ../Util.h ../png.h
	$(GXX) -std=c++0x -Wall -O3 process.cpp seam_carving.o -o process -lpthread -ldl $(PNGFLAGS) $(CUDA_LFLAGS)

out.png: process
	./process input.png 8 out.png

clean:
	rm process seam_carving.o seam_carving.bc seam_carving

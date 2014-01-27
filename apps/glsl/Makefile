GXX ?= g++
CXXFLAGS += -g -O0 -I../../include
OPENGL_LIBS = -lX11 -lGL


opengl_test: opengl_test.o ycc.o blur.o
	g++ $(CXXFLAGS) $^ -o $@  $(OPENGL_LIBS)

.PHONY: clean
clean:
	rm -f opengl_test
	rm -f test halide_blur.o halide_blur ycc.o blur.o

blur.o: halide_blur
	HL_TARGET=host-opengl ./halide_blur

ycc.o: halide_ycc
	HL_TARGET=host-opengl ./halide_ycc

halide_blur: ../../bin/libHalide.a halide_blur.o
	$(MAKE) -C ../../ bin/libHalide.a
	$(GXX) halide_blur.o -I ../../include ../../bin/libHalide.a -o halide_blur -ldl -lpthread

halide_ycc: ../../bin/libHalide.a halide_ycc.o
	$(MAKE) -C ../../ bin/libHalide.a
	$(GXX) halide_ycc.o -I ../../include ../../bin/libHalide.a -o halide_ycc -ldl -lpthread


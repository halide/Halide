CXX ?= g++
CXXFLAGS += -g -O0 -I../../include

UNAME = $(shell uname)
ifeq ($(UNAME), Linux)
OPENGL_LIBS = -lX11 -lGL
endif
ifeq ($(UNAME), Darwin)
OPENGL_LIBS = -framework AGL
endif

HALIDE_LIBS = ../../bin/libHalide.a -ldl -lpthread
HALIDE_LIBS += -lcurses

.PHONY: clean

opengl_test: blur.o ycc.o opengl_test.o OpenGLSupport.o
	$(CXX) $(CXXFLAGS) $^ -o $@  $(OPENGL_LIBS)

blur.h blur.o: halide_blur
	HL_TARGET=host-opengl-gpu_debug ./halide_blur

ycc.h ycc.o: halide_ycc
	HL_TARGET=host-opengl-gpu_debug ./halide_ycc

halide_blur: ../../bin/libHalide.a halide_blur.o
	$(MAKE) -C ../../ bin/libHalide.a
	$(CXX) halide_blur.o -o halide_blur $(CXXFLAGS) $(HALIDE_LIBS)

halide_ycc: ../../bin/libHalide.a halide_ycc.o
	$(MAKE) -C ../../ bin/libHalide.a
	$(CXX) halide_ycc.o -o halide_ycc $(CXXFLAGS) $(HALIDE_LIBS)

clean:
	rm -f opengl_test
	rm -f test halide_blur.o halide_blur ycc.o blur.o OpenGLSupport.o

include ../support/Makefile.inc

CXXFLAGS += -g -O0

UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
OPENGL_LIBS = -lX11 -lGL
endif

HALIDE_LIBS = $(LIB_HALIDE) -ldl $(LLVM_LDFLAGS)

all: opengl_test

run: opengl_test
	LD_LIBRARY_PATH=../../bin ./opengl_test

opengl_test: blur.a ycc.a opengl_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ -ldl -lpthread -L$(TOP)/bin $(OPENGL_LIBS)

blur.h blur.a: halide_blur
	HL_TARGET=host-opengl-debug ./halide_blur

ycc.h ycc.a: halide_ycc
	HL_TARGET=host-opengl-debug ./halide_ycc

halide_blur: halide_blur.cpp $(HALIDE_LIB)
	$(CXX) $(CXXFLAGS) $< -o $@ $(CXXFLAGS) $(HALIDE_LIBS)

halide_ycc: halide_ycc.cpp $(HALIDE_LIB)
	$(CXX) $(CXXFLAGS) $< -o $@ $(CXXFLAGS) $(HALIDE_LIBS)

.PHONY: clean
clean:
	rm -f opengl_test test_jit halide_blur halide_ycc *.o *.a blur.h ycc.h

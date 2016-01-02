include ../support/Makefile.inc

TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
.PHONY: all $(TOP)
all:
HALIDE_LIB := $(TOP)/$(LIB_HALIDE)
$(HALIDE_LIB): $(TOP)
	$(MAKE) -C $(TOP) $(LIB_HALIDE)

CXXFLAGS += -g -O0

UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
OPENGL_LIBS = -lX11 -lGL
endif

HALIDE_LIBS = $(HALIDE_LIB) -ldl $(LLVM_LDFLAGS)

all: opengl_test

run: opengl_test
	LD_LIBRARY_PATH=../../bin ./opengl_test

opengl_test: blur.o ycc.o opengl_test.o
	$(CXX) $(CXXFLAGS) $^ -o $@ -ldl -lpthread -L$(TOP)/bin $(OPENGL_LIBS)

blur.h blur.o: halide_blur
	HL_TARGET=host-opengl-gpu_debug ./halide_blur

ycc.h ycc.o: halide_ycc
	HL_TARGET=host-opengl-gpu_debug ./halide_ycc

halide_blur: halide_blur.o $(HALIDE_LIB)
	$(CXX) $(CXXFLAGS) $< -o $@ $(CXXFLAGS) $(HALIDE_LIBS)

halide_ycc: halide_ycc.o $(HALIDE_LIB)
	$(CXX) $(CXXFLAGS) $< -o $@ $(CXXFLAGS) $(HALIDE_LIBS)

.PHONY: clean
clean:
	rm -f opengl_test test_jit halide_blur halide_ycc *.o blur.h ycc.h

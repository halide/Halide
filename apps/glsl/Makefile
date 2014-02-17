include ../support/Makefile.inc

TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
.PHONY: all $(TOP)
all:
HALIDE_LIB := $(TOP)/$(LIB_HALIDE)
$(HALIDE_LIB): $(TOP)
	$(MAKE) -C $(TOP) $(LIB_HALIDE)

CXXFLAGS += -g -O0 -I../../include

UNAME = $(shell uname)
ifeq ($(UNAME), Linux)
OPENGL_LIBS = -lX11 -lGL
endif
ifeq ($(UNAME), Darwin)
OPENGL_LIBS = -framework AGL
endif

HALIDE_LIBS = $(HALIDE_LIB) -ldl -lpthread
HALIDE_LIBS += -lcurses

all: opengl_test

OBJS = blur.o ycc.o opengl_test.o OpenGLSupport.o

opengl_test: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@  $(OPENGL_LIBS)

blur.h blur.o: halide_blur
	HL_TARGET=host-opengl-gpu_debug ./halide_blur

ycc.h ycc.o: halide_ycc
	HL_TARGET=host-opengl-gpu_debug ./halide_ycc

halide_blur: halide_blur.o $(HALIDE_LIB)
	$(CXX) $< -o $@ $(CXXFLAGS) $(HALIDE_LIBS)

halide_ycc: halide_ycc.o $(HALIDE_LIB)
	$(CXX) $< -o $@ $(CXXFLAGS) $(HALIDE_LIBS)

.PHONY: clean
clean:
	rm -f opengl_test halide_blur halide_ycc *.o blur.h ycc.h

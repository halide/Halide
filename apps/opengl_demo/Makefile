#
# This could be more DRY using some Makefile magic, but for the example
# app will try to maximize clarity by making most rules explicit
#

# Where to find Halide.
#
# If you are building this demo using Halide installed systemwide (e.g. on
# OS X installed via homebrew), you can set:
#
#  HALIDE_TOOLS_DIR = /usr/local/share/halide/tools
#  HALIDE_LIB_PATH =
#  HALIDE_INC_PATH =
#
# These settings are for building within the Halide source tree:
HALIDE_TOOLS_DIR = ../../tools
HALIDE_LIB_PATH  = -L ../../bin
HALIDE_INC_PATH  = -I ../../include

# Platform-specific settings.
#
UNAME = $(shell uname)

ifeq ($(UNAME),Darwin)

  # These are for OS X:
  DTX_FONT       = /Library/Fonts/Arial.ttf
  OPENGL_LIBS    = -lglfw3 -framework OpenGL -framework GLUT
  GENERATOR_LIBS = -lHalide -lz -lcurses

else

  # These are for Ubuntu Linux
  DTX_FONT       = /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
  OPENGL_LIBS    = `pkg-config glfw3 --libs` -lGL -lglut -lX11 -lpthread -ldl -lXxf86vm -lXinerama -lXcursor -lXrandr
  GENERATOR_LIBS = -lHalide -lz -lcurses -Wl,--rpath=$(HALIDE_LIB_PATH)

endif

#
# General build settings.  Should be good cross-platform.
#
MAIN_LIBS      = -lpng -ldrawtext $(OPENGL_LIBS)
GENERATOR_LIBS = -lHalide -lz -lcurses
CXXFLAGS       = -std=c++11 -g -DDTX_FONT=\"$(DTX_FONT)\" $(HALIDE_INC_PATH)

.PHONY: run clean

default:	run

run:	build/opengl_demo
	build/opengl_demo image.png

clean:
	rm -rf build

build/opengl_demo: \
    build/main.o \
    build/layout.o \
    build/timer.o \
    build/glfw_helpers.o \
    build/opengl_helpers.o \
    build/png_helpers.o \
    build/sample_filter_cpu.o \
    build/sample_filter_opengl.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(MAIN_LIBS)

#
# Explicitly list the dependency on the generated filter header files,
# to ensure that they are created first.
#
build/main.o: \
    build/sample_filter_cpu.h \
    build/sample_filter_opengl.h

#
# Rules to AOT-compile the halide filter for both CPU and OpenGL; the
# compiled filters depend on build/generate_sample_filter, which in turn
# depends on the halide filter source in sample_filter.cpp
#
build/sample_filter_cpu.o build/sample_filter_cpu.h: build/generate_sample_filter
	LD_LIBRARY_PATH=../../bin build/generate_sample_filter -e o,h,stmt -o build -f sample_filter_cpu target=host

build/sample_filter_opengl.o build/sample_filter_opengl.h: build/generate_sample_filter
	LD_LIBRARY_PATH=../../bin build/generate_sample_filter -e o,h,stmt -o build -f sample_filter_opengl target=host-opengl-debug

build/generate_sample_filter: sample_filter.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -fno-rtti -o $@ $^ $(HALIDE_TOOLS_DIR)/GenGen.cpp $(HALIDE_LIB_PATH) $(GENERATOR_LIBS)

#
# Build in subdir using auto-dependency mechanism
#
build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) -c $(CXXFLAGS) -MMD -MF $(patsubst %.o,%.d,$@) -o $@ $<

-include $(wildcard build/*.d)

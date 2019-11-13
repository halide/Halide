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
HL_TARGET ?= host

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

# Output directory.
BIN ?= bin

.PHONY: run clean

default:	run

run:	$(BIN)/opengl_demo
	$(BIN)/opengl_demo image.png

clean:
	rm -rf $(BIN)

$(BIN)/opengl_demo: \
    $(BIN)/main.o \
    $(BIN)/layout.o \
    $(BIN)/timer.o \
    $(BIN)/glfw_helpers.o \
    $(BIN)/opengl_helpers.o \
    $(BIN)/png_helpers.o \
    $(BIN)/sample_filter_cpu.o \
    $(BIN)/sample_filter_opengl.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(MAIN_LIBS)

#
# Explicitly list the dependency on the generated filter header files,
# to ensure that they are created first.
#
$(BIN)/main.o: \
    $(BIN)/sample_filter_cpu.h \
    $(BIN)/sample_filter_opengl.h

#
# Rules to AOT-compile the halide filter for both CPU and OpenGL; the
# compiled filters depend on $(BIN)/sample_filter.generator, which in turn
# depends on the halide filter source in sample_filter.cpp
#
$(BIN)/sample_filter_cpu.o $(BIN)/sample_filter_cpu.h: $(BIN)/sample_filter.generator
	LD_LIBRARY_PATH=../../bin $(BIN)/sample_filter.generator -g sample_filter -e object,c_header,stmt -o $(BIN) -f sample_filter_cpu target=$(HL_TARGET)

$(BIN)/sample_filter_opengl.o $(BIN)/sample_filter_opengl.h: $(BIN)/sample_filter.generator
	LD_LIBRARY_PATH=../../bin $(BIN)/sample_filter.generator -g sample_filter -e object,c_header,stmt -o $(BIN) -f sample_filter_opengl target=host-opengl-debug

$(BIN)/sample_filter.generator: sample_filter_generator.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(HALIDE_TOOLS_DIR)/GenGen.cpp $(HALIDE_LIB_PATH) $(GENERATOR_LIBS) $(HALIDE_SYSTEM_LIBS)

#
# Build in subdir using auto-dependency mechanism
#
$(BIN)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) -I$(BIN) -MMD -MF $(patsubst %.o,%.d,$@) -o $@ $<

-include $(wildcard $(BIN)/*.d)

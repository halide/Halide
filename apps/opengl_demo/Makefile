#
# This could be more DRY using some Makefile magic, but for the example
# app will try to maximize clarity by making most rules explicit
#

# OS X settings, with Halide and other libraries installed by homebrew
HALIDE_TOOLS_DIR = /usr/local/share/halide/tools
MAIN_LIBS	 = -framework OpenGL -framework GLUT -lglfw3 -lpng -ldrawtext
GENERATOR_LIBS	 = -lHalide -lz -lcurses
DTX_FONT	 = /Library/Fonts/Arial.ttf

CXXFLAGS = -std=c++11 -g -DDTX_FONT=\"$(DTX_FONT)\"

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
	build/generate_sample_filter -e o,h,stmt -o build -f sample_filter_cpu target=host

build/sample_filter_opengl.o build/sample_filter_opengl.h: build/generate_sample_filter
	build/generate_sample_filter -e o,h,stmt -o build -f sample_filter_opengl target=host-opengl-debug

build/generate_sample_filter: sample_filter.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -fno-rtti -o $@ $^ $(HALIDE_TOOLS_DIR)/GenGen.cpp $(GENERATOR_LIBS)

#
# Build in subdir using auto-dependency mechanism
#
build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) -c $(CXXFLAGS) -MMD -MF $(patsubst %.o,%.d,$@) -o $@ $<

-include $(wildcard build/*.d)

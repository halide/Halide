include ../support/Makefile.inc

BIN ?== bin
CXXFLAGS += -g -O0

UNAME := $(shell uname)
ifeq ($(UNAME), Linux)
OPENGL_LIBS = -lX11 -lGL
endif

HALIDE_LIBS = $(LIB_HALIDE) -ldl $(LLVM_LDFLAGS)

all: $(BIN)/opengl_test

$(BIN)/halide_blur_glsl_exec: halide_blur_glsl_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/halide_blur_glsl.a: $(BIN)/halide_blur_glsl_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN) target=host-opengl-debug

$(BIN)/halide_ycc_glsl_exec: halide_ycc_glsl_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/halide_ycc_glsl.a: $(BIN)/halide_ycc_glsl_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN) target=host-opengl-debug

$(BIN)/opengl_test: opengl_test.cpp $(BIN)/halide_blur_glsl.a $(BIN)/halide_ycc_glsl.a
	$(CXX) $(CXXFLAGS) -I$(BIN) $^ -o $@ $(LDFLAGS) -L$(TOP)/bin $(OPENGL_LIBS)

run: $(BIN)/opengl_test
	LD_LIBRARY_PATH=../../bin $(BIN)/opengl_test

.PHONY: clean
clean:
	rm -rf $(BIN)

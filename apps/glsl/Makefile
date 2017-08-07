include ../support/Makefile.inc

CXXFLAGS += -g -O0

all: $(BIN)/opengl_test

$(BIN)/halide_blur_glsl_exec: halide_blur_glsl_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/halide_blur_glsl.a: $(BIN)/halide_blur_glsl_exec
	@mkdir -p $(@D)
	$^ -o $(BIN) target=host-opengl-debug

$(BIN)/halide_ycc_glsl_exec: halide_ycc_glsl_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/halide_ycc_glsl.a: $(BIN)/halide_ycc_glsl_exec
	@mkdir -p $(@D)
	$^ -o $(BIN) target=host-opengl-debug

$(BIN)/opengl_test: opengl_test.cpp $(BIN)/halide_blur_glsl.a $(BIN)/halide_ycc_glsl.a
	$(CXX) $(CXXFLAGS) -I$(BIN) $^ -o $@ $(LDFLAGS) -L$(TOP)/bin $(PLATFORM_OPENGL_LDFLAGS)

run: $(BIN)/opengl_test
	LD_LIBRARY_PATH=../../bin $(BIN)/opengl_test

.PHONY: clean
clean:
	rm -rf $(BIN)

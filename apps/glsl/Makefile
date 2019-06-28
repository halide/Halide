include ../support/Makefile.inc

# Note: using the -g flag in conjunction with the -debug Feature on OSX may
# produce "failed to insert symbol" warnings at link time; this is annoying but harmless.
CXXFLAGS += -g -O0

all: $(BIN)/$(HL_TARGET)/opengl_test

$(GENERATOR_BIN)/halide_blur_glsl.generator: halide_blur_glsl_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/halide_blur_glsl.a: $(GENERATOR_BIN)/halide_blur_glsl.generator
	@mkdir -p $(@D)
	$^ -g halide_blur_glsl -e $(GENERATOR_OUTPUTS) -o $(@D) target=$*-opengl-debug

$(GENERATOR_BIN)/halide_ycc_glsl.generator: halide_ycc_glsl_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/halide_ycc_glsl.a: $(GENERATOR_BIN)/halide_ycc_glsl.generator
	@mkdir -p $(@D)
	$^ -g halide_ycc_glsl -e $(GENERATOR_OUTPUTS) -o $(@D) target=$*-opengl-debug

$(BIN)/%/opengl_test: opengl_test.cpp $(BIN)/%/halide_blur_glsl.a $(BIN)/%/halide_ycc_glsl.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $^ -o $@ $(LDFLAGS) -L$(TOP)/bin $(PLATFORM_OPENGL_LDFLAGS)

run: $(BIN)/$(HL_TARGET)/opengl_test
	LD_LIBRARY_PATH=../../bin $<

test: run

.PHONY: clean
clean:
	rm -rf $(BIN)

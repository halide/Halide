include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/iir_blur.generator: iir_blur_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(GENERATOR_LDFLAGS)

$(BIN)/%/iir_blur.a: $(GENERATOR_BIN)/iir_blur.generator
	@mkdir -p $(@D)
	$< -g iir_blur -f iir_blur -o $(BIN)/$* target=$*-no_runtime auto_schedule=false

$(BIN)/%/iir_blur_auto_schedule.a: $(GENERATOR_BIN)/iir_blur.generator
	@mkdir -p $(@D)
	$< -g iir_blur -f iir_blur_auto_schedule -o $(BIN)/$* target=$*-no_runtime auto_schedule=true

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/iir_blur.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

$(BIN)/%/filter: filter.cpp $(BIN)/%/iir_blur.a $(BIN)/%/iir_blur_auto_schedule.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< ../images/rgba.png $(BIN)/$*/out.png

clean:
	rm -rf $(BIN)

include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/iir_blur.generator: iir_blur_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(GENERATOR_LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/%/iir_blur.a: $(GENERATOR_BIN)/iir_blur.generator
	@mkdir -p $(@D)
	$< -g iir_blur -f iir_blur -o $(BIN)/$* target=$* auto_schedule=false

$(BIN)/%/iir_blur_auto_schedule.a: $(GENERATOR_BIN)/iir_blur.generator
	@mkdir -p $(@D)
	$< -g iir_blur -f iir_blur_auto_schedule -o $(BIN)/$* target=$* auto_schedule=true 

$(BIN)/%/filter: filter.cpp $(BIN)/%/iir_blur.a $(BIN)/%/iir_blur_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< ../images/rgb.png $(BIN)/$*/out.png

clean:
	rm -rf $(BIN)

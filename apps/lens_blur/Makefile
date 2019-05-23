include ../support/Makefile.inc


all: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/lens_blur.generator: lens_blur_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/%/lens_blur.a: $(GENERATOR_BIN)/lens_blur.generator
	@mkdir -p $(@D)
	$^ -g lens_blur -o $(@D) -f lens_blur target=$* auto_schedule=false

$(BIN)/%/lens_blur_auto_schedule.a: $(GENERATOR_BIN)/lens_blur.generator
	@mkdir -p $(@D)
	$^ -g lens_blur -o $(@D) -f lens_blur_auto_schedule target=$*-no_runtime auto_schedule=true

$(BIN)/%/process: process.cpp $(BIN)/%/lens_blur.a $(BIN)/%/lens_blur_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb_small.png 32 13 0.5 32 3 $@

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png

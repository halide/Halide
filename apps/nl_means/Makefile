include ../support/Makefile.inc


all: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/nl_means.generator: nl_means_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/%/nl_means.a: $(GENERATOR_BIN)/nl_means.generator
	@mkdir -p $(@D)
	$^ -g nl_means -o $(@D) -f nl_means target=$* auto_schedule=false

$(BIN)/%/nl_means_auto_schedule.a: $(GENERATOR_BIN)/nl_means.generator
	@mkdir -p $(@D)
	$^ -g nl_means -o $(@D) -f nl_means_auto_schedule target=$*-no_runtime auto_schedule=true

$(BIN)/%/process: process.cpp $(BIN)/%/nl_means.a $(BIN)/%/nl_means_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb.png 7 7 0.12 10 $@

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png

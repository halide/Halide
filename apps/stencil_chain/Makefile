include ../support/Makefile.inc


all: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/stencil_chain.generator: stencil_chain_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/stencil_chain.a: $(GENERATOR_BIN)/stencil_chain.generator
	@mkdir -p $(@D)
	$^ -g stencil_chain -o $(@D) -f stencil_chain target=$* auto_schedule=false

$(BIN)/%/stencil_chain_auto_schedule.a: $(GENERATOR_BIN)/stencil_chain.generator
	@mkdir -p $(@D)
	$^ -g stencil_chain -o $(@D) -f stencil_chain_auto_schedule target=$*-no_runtime auto_schedule=true

$(BIN)/%/process: process.cpp $(BIN)/%/stencil_chain.a $(BIN)/%/stencil_chain_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb.png 10 $@

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png

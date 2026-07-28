include ../support/Makefile.inc


# The wmma instructions require compute capability 7.0 or above.
TENSORCORE_TARGET ?= host-cuda-cuda_capability_80

all: $(BIN)/$(HL_TARGET)/runner

$(GENERATOR_BIN)/resize.generator: resize_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/resize_cudaonly.a: $(GENERATOR_BIN)/resize.generator
	@mkdir -p $(@D)
	$^ -g resize -f resize_cudaonly -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(TENSORCORE_TARGET) gpu_schedule=cudaonly

$(BIN)/%/resize_tensorcore.a: $(GENERATOR_BIN)/resize.generator
	@mkdir -p $(@D)
	$^ -g resize -f resize_tensorcore -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(TENSORCORE_TARGET) gpu_schedule=tensorcore

$(BIN)/%/runner: runner.cpp $(BIN)/%/resize_cudaonly.a $(BIN)/%/resize_tensorcore.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) $(LIBHALIDE_LDFLAGS)

test: $(BIN)/$(HL_TARGET)/runner
	$^

clean:
	rm -rf $(BIN)

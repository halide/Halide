include ../support/Makefile.inc

.PHONY: build clean test
.SECONDARY:

build: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/local_laplacian.generator: local_laplacian_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/local_laplacian.a: $(GENERATOR_BIN)/local_laplacian.generator
	@mkdir -p $(@D)
	$^ -g local_laplacian -e $(GENERATOR_OUTPUTS) -o $(@D) -f local_laplacian target=$*

$(BIN)/%/local_laplacian_auto_schedule.a: $(GENERATOR_BIN)/local_laplacian.generator
	@mkdir -p $(@D)
	$^ -g local_laplacian -e $(GENERATOR_OUTPUTS) -o $(@D) -f local_laplacian_auto_schedule target=$*-no_runtime autoscheduler=Mullapudi2016

$(BIN)/%/process: process.cpp $(BIN)/%/local_laplacian.a $(BIN)/%/local_laplacian_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb.png 8 1 1 10 $@

$(BIN)/%/out.tiff: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb.png 8 1 1 10 $@

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png $(BIN)/$(HL_TARGET)/out.tiff

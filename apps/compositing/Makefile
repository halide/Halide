include ../support/Makefile.inc

.PHONY: build clean test

build: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/compositing.generator: compositing_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/compositing.a: $(GENERATOR_BIN)/compositing.generator
	@mkdir -p $(@D)
	$^ -g compositing -e $(GENERATOR_OUTPUTS) -o $(@D) -f compositing target=$*

$(BIN)/%/compositing_auto_schedule.a: $(GENERATOR_BIN)/compositing.generator
	@mkdir -p $(@D)
	$^ -g compositing -e $(GENERATOR_OUTPUTS) -o $(@D) -f compositing_auto_schedule target=$*-no_runtime autoscheduler=Mullapudi2016

$(BIN)/%/process: process.cpp $(BIN)/%/compositing.a $(BIN)/%/compositing_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgba.png 10 $@

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png

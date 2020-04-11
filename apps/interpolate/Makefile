include ../support/Makefile.inc

AUTOSCHED_SRC = $(realpath ../autoscheduler)
AUTOSCHED_BIN = $(realpath ../autoscheduler/bin)

.PHONY: build clean test

build: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/interpolate.generator: interpolate_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/interpolate.a: $(GENERATOR_BIN)/interpolate.generator
	@mkdir -p $(@D)
	$< -g interpolate -f interpolate -o $(BIN)/$* target=$*-no_runtime auto_schedule=false

$(BIN)/%/interpolate_auto_schedule.a: $(GENERATOR_BIN)/interpolate.generator
	@mkdir -p $(@D)
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/baseline.weights \
	$< -g interpolate -f interpolate_auto_schedule -o $(BIN)/$* target=$*-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so -s Adams2019

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/interpolate.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

$(BIN)/%/filter: filter.cpp $(BIN)/%/interpolate.a $(BIN)/%/interpolate_auto_schedule.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< ../images/rgba.png $(BIN)/$*/out.png

clean:
	rm -rf $(BIN)

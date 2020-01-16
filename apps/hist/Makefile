include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/hist.generator: hist_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(GENERATOR_LDFLAGS)

$(BIN)/%/hist.a: $(GENERATOR_BIN)/hist.generator
	@mkdir -p $(@D)
	$< -g hist -f hist -o $(BIN)/$* target=$*-no_runtime auto_schedule=false

$(BIN)/%/hist_auto_schedule.a: $(GENERATOR_BIN)/hist.generator
	@mkdir -p $(@D)
	$< -g hist -f hist_auto_schedule -o $(BIN)/$* target=$*-no_runtime auto_schedule=true

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/hist.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

$(BIN)/%/filter: filter.cpp $(BIN)/%/hist.a $(BIN)/%/hist_auto_schedule.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< ../images/rgba.png $(BIN)/$*/out.png

clean:
	rm -rf $(BIN)

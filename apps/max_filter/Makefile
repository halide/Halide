include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/max_filter.generator: max_filter_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(GENERATOR_LDFLAGS)

$(BIN)/%/max_filter.a: $(GENERATOR_BIN)/max_filter.generator
	@mkdir -p $(@D)
	$< -g max_filter -f max_filter -o $(BIN)/$* target=$*-no_runtime auto_schedule=false

$(BIN)/%/max_filter_auto_schedule.a: $(GENERATOR_BIN)/max_filter.generator
	@mkdir -p $(@D)
	$< -g max_filter -f max_filter_auto_schedule -o $(BIN)/$* target=$*-no_runtime auto_schedule=true

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/max_filter.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

$(BIN)/%/filter: filter.cpp $(BIN)/%/max_filter.a $(BIN)/%/max_filter_auto_schedule.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< ../images/rgba.png $(BIN)/$*/out.png

clean:
	rm -rf $(BIN)

include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/bgu.generator: bgu_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(GENERATOR_LDFLAGS)

$(BIN)/%/bgu.a: $(GENERATOR_BIN)/bgu.generator
	@mkdir -p $(@D)
	$< -g bgu -f bgu -o $(BIN)/$* target=$*-no_runtime auto_schedule=false

$(BIN)/%/bgu_auto_schedule.a: $(GENERATOR_BIN)/bgu.generator
	@mkdir -p $(@D)
	$< -g bgu -f bgu_auto_schedule -o $(BIN)/$* target=$*-no_runtime auto_schedule=true

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/bgu.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

$(BIN)/%/filter: filter.cpp $(BIN)/%/bgu.a $(BIN)/%/bgu_auto_schedule.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< ../images/rgb.png $(BIN)/$*/out.png

clean:
	rm -rf $(BIN)

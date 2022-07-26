include ../support/Makefile.inc

.PHONY: build clean test

build: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

# .SECONDARY with no prerequisites causes all targets to be treated as secondary
# (i.e., no target is removed because it is considered intermediate).
.SECONDARY:

$(GENERATOR_BIN)/bgu.generator: bgu_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/bgu.a: $(GENERATOR_BIN)/bgu.generator
	@mkdir -p $(@D)
	$< -g bgu -f bgu -o $(BIN)/$* target=$*-no_runtime

$(BIN)/%/bgu_auto_schedule.a: $(GENERATOR_BIN)/bgu.generator
	@mkdir -p $(@D)
	$< -g bgu -f bgu_auto_schedule -o $(BIN)/$* target=$*-no_runtime autoscheduler=Mullapudi2016

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/bgu.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

$(BIN)/%/filter: filter.cpp $(BIN)/%/bgu.a $(BIN)/%/bgu_auto_schedule.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< ../images/rgb.png $(BIN)/$*/out.png

clean:
	rm -rf $(BIN)

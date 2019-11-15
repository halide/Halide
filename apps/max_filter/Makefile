include ../support/Makefile.inc
include ../support/autoscheduler.inc

all: test

$(GENERATOR_BIN)/max_filter.generator: max_filter_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/%/max_filter.a: $(GENERATOR_BIN)/max_filter.generator
	@mkdir -p $(@D)
	$< -g max_filter -f max_filter -o $(BIN)/$* target=$* auto_schedule=false

$(BIN)/%/max_filter_classic_auto_schedule.a: $(GENERATOR_BIN)/max_filter.generator
	@mkdir -p $(@D)
	$< -g max_filter -f max_filter_classic_auto_schedule -o $(BIN)/$* target=$* auto_schedule=true machine_params=$(HL_MACHINE_PARAMS)

$(BIN)/%/max_filter_auto_schedule.a: $(GENERATOR_BIN)/max_filter.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/weights \
	$< -g max_filter -f max_filter_auto_schedule -o $(BIN)/$* target=$* auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so

$(BIN)/%/filter: filter.cpp $(BIN)/%/max_filter.a $(BIN)/%/max_filter_auto_schedule.a $(BIN)/%/max_filter_classic_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/filter
	$(BIN)/$(HL_TARGET)/filter ../images/rgb.png $(BIN)/out.tiff


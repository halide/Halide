include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/interpolate.generator: interpolate_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(GENERATOR_LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/%/interpolate.a: $(GENERATOR_BIN)/interpolate.generator
	@mkdir -p $(@D)
	$< -g interpolate -f interpolate -o $(BIN)/$* target=$* auto_schedule=false

$(BIN)/%/interpolate_auto_schedule.a: $(GENERATOR_BIN)/interpolate.generator
	@mkdir -p $(@D)
	$< -g interpolate -f interpolate_auto_schedule -o $(BIN)/$* target=$* auto_schedule=true 

$(BIN)/%/filter: filter.cpp $(BIN)/%/interpolate.a $(BIN)/%/interpolate_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< ../images/rgba.png $(BIN)/$*/out.png

clean:
	rm -rf $(BIN)

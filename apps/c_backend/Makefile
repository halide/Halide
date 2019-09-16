include ../support/Makefile.inc

test: $(BIN)/$(HL_TARGET)/run $(BIN)/$(HL_TARGET)/run_cpp
	$(BIN)/$(HL_TARGET)/run
	$(BIN)/$(HL_TARGET)/run_cpp

all: $(BIN)/$(HL_TARGET)/test

$(GENERATOR_BIN)/pipeline.generator: pipeline_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/pipeline_native.a: $(GENERATOR_BIN)/pipeline.generator
	@mkdir -p $(@D)
	$^ -g pipeline -o $(@D) -f pipeline_native -e $(GENERATOR_OUTPUTS) target=$*

$(BIN)/%/pipeline_c.halide_generated.cpp: $(GENERATOR_BIN)/pipeline.generator
	@mkdir -p $(@D)
	$^ -g pipeline -o $(@D) -f pipeline_c -e c_source,c_header target=$*

$(BIN)/%/run: run.cpp $(BIN)/%/pipeline_c.halide_generated.cpp $(BIN)/%/pipeline_native.a
	$(CXX) $(CXXFLAGS) -Wall -I$(BIN)/$* $(filter-out %.h,$^) -o $@  $(LDFLAGS)

$(GENERATOR_BIN)/pipeline_cpp.generator: pipeline_cpp_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/pipeline_cpp_cpp.halide_generated.cpp: $(GENERATOR_BIN)/pipeline_cpp.generator
	@mkdir -p $(@D)
	$^ -g pipeline_cpp -o $(@D) -f pipeline_cpp_cpp -e c_source,c_header target=host-c_plus_plus_name_mangling

$(BIN)/%/pipeline_cpp_native.a: $(GENERATOR_BIN)/pipeline_cpp.generator
	@mkdir -p $(@D)
	$^ -g pipeline_cpp -o $(@D) -f pipeline_cpp_native -e $(GENERATOR_OUTPUTS) target=host-c_plus_plus_name_mangling

$(BIN)/%/run_cpp: run_cpp.cpp $(BIN)/%/pipeline_cpp_cpp.halide_generated.cpp $(BIN)/%/pipeline_cpp_native.a
	$(CXX) $(CXXFLAGS) -Wall -I$(BIN)/$* $(filter-out %.h,$^) -o $@  $(LDFLAGS)

clean:
	rm -rf $(BIN)

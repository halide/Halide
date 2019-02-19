include ../support/Makefile.inc

test: $(BIN)/run $(BIN)/run_cpp
	$(BIN)/run
	$(BIN)/run_cpp

all: $(BIN)/test

$(BIN)/pipeline.generator: pipeline_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/pipeline_native.a: $(BIN)/pipeline.generator
	@mkdir -p $(@D)
	$^ -g pipeline -o $(BIN) -f pipeline_native -e static_library,h target=$(HL_TARGET)

$(BIN)/pipeline_c.cpp: $(BIN)/pipeline.generator
	@mkdir -p $(@D)
	$^ -g pipeline -o $(BIN) -f pipeline_c -e cpp,h target=$(HL_TARGET)

$(BIN)/run: run.cpp $(BIN)/pipeline_c.cpp $(BIN)/pipeline_native.a
	$(CXX) $(CXXFLAGS) -Wall -I$(BIN) $(filter-out %.h,$^) -o $@  $(LDFLAGS)

$(BIN)/pipeline_cpp.generator: pipeline_cpp_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/pipeline_cpp_cpp.cpp: $(BIN)/pipeline_cpp.generator
	@mkdir -p $(@D)
	$^ -g pipeline_cpp -o $(BIN) -f pipeline_cpp_cpp -e cpp,h target=host-c_plus_plus_name_mangling

$(BIN)/pipeline_cpp_native.a: $(BIN)/pipeline_cpp.generator
	@mkdir -p $(@D)
	$^ -g pipeline_cpp -o $(BIN) -f pipeline_cpp_native -e static_library,h target=host-c_plus_plus_name_mangling

$(BIN)/run_cpp: run_cpp.cpp $(BIN)/pipeline_cpp_cpp.cpp $(BIN)/pipeline_cpp_native.a
	$(CXX) $(CXXFLAGS) -Wall -I$(BIN) $(filter-out %.h,$^) -o $@  $(LDFLAGS)

clean:
	rm -rf $(BIN)

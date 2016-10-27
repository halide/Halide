include ../support/Makefile.inc

test: $(BIN)/run $(BIN)/run_cpp
	$(BIN)/run
	$(BIN)/run_cpp

all: $(BIN)/test

$(BIN)/pipeline_exec: pipeline_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/pipeline_native.a: $(BIN)/pipeline_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN) -f pipeline_native -e static_library,h target=$(HL_TARGET)

$(BIN)/pipeline_c.cpp: $(BIN)/pipeline_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN) -f pipeline_c -e cpp,h target=$(HL_TARGET)

$(BIN)/run: run.cpp $(BIN)/pipeline_c.cpp $(BIN)/pipeline_native.a
	$(CXX) $(CXXFLAGS) -Wall -I$(BIN) $(filter-out %.h,$^) -o $@  $(LDFLAGS)

$(BIN)/pipeline_cpp_exec: pipeline_cpp_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/pipeline_cpp_cpp.cpp: $(BIN)/pipeline_cpp_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN) -f pipeline_cpp_cpp -e cpp,h target=host-c_plus_plus_name_mangling

$(BIN)/pipeline_cpp_native.a: $(BIN)/pipeline_cpp_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN) -f pipeline_cpp_native -e static_library,h target=host-c_plus_plus_name_mangling

$(BIN)/run_cpp: run_cpp.cpp $(BIN)/pipeline_cpp_cpp.cpp $(BIN)/pipeline_cpp_native.a
	$(CXX) $(CXXFLAGS) -Wall -I$(BIN) $(filter-out %.h,$^) -o $@  $(LDFLAGS)

clean:
	rm -rf $(BIN)

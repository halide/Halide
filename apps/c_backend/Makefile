include ../support/Makefile.inc

test: $(BIN)/run $(BIN)/run_cpp
	$(BIN)/run
	$(BIN)/run_cpp

all: $(BIN)/test

$(BIN)/pipeline_c.cpp: pipeline_generator.cpp
	@-mkdir -p $(BIN)
	$(HALIDE_SRC_PATH)/tools/gengen.sh -c $(CXX) -l $(LIB_HALIDE) -f pipeline_c -e cpp,h -o $(BIN) -s $^ target=host

$(BIN)/pipeline_native.a: pipeline_generator.cpp
	@-mkdir -p $(BIN)
	$(HALIDE_SRC_PATH)/tools/gengen.sh -c $(CXX) -l $(LIB_HALIDE) -f pipeline_native -e static_library,h -o $(BIN) -s $^ target=host

$(BIN)/run: run.cpp $(BIN)/pipeline_c.cpp $(BIN)/pipeline_native.a
	$(CXX) $(CXXFLAGS) -Wall -I$(BIN) $(filter-out %.h,$^) $(LDFLAGS) -o $@ 

$(BIN)/pipeline_cpp_cpp.cpp: pipeline_cpp_generator.cpp
	@-mkdir -p $(BIN)
	$(HALIDE_SRC_PATH)/tools/gengen.sh -c $(CXX) -l $(LIB_HALIDE) -f pipeline_cpp_cpp -e cpp,h -o $(BIN) -s $^ target=host-c_plus_plus_name_mangling

$(BIN)/pipeline_cpp_native.a: pipeline_cpp_generator.cpp
	@-mkdir -p $(BIN)
	$(HALIDE_SRC_PATH)/tools/gengen.sh -c $(CXX) -l $(LIB_HALIDE) -f pipeline_cpp_native -e static_library,h -o $(BIN) -s $^ target=host-c_plus_plus_name_mangling

$(BIN)/run_cpp: run_cpp.cpp $(BIN)/pipeline_cpp_cpp.cpp $(BIN)/pipeline_cpp_native.a
	$(CXX) $(CXXFLAGS) -Wall -I$(BIN) $(filter-out %.h,$^) $(LDFLAGS) -o $@ 

clean:
	rm -rf $(BIN)

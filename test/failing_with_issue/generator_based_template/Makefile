# Makefile inspired by the structure of the apps/

HALIDE_DISTRIB_PATH ?= ../../../distrib
include ../../../apps/support/Makefile.inc

.PHONY: build clean test
codegen: $(BIN)/$(HL_TARGET)/generated_pipeline.a


$(GENERATOR_BIN)/generator: generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/$(HL_TARGET)/runtime.a: $(GENERATOR_BIN)/generator
	@mkdir -p $(@D)
	$^ -r runtime -o $(@D) -e static_library target=$(HL_TARGET)

$(BIN)/$(HL_TARGET)/generated_pipeline.a: $(GENERATOR_BIN)/generator
	@mkdir -p $(@D)
	$^ -g test_generator -e stmt,stmt_html,conceptual_stmt,conceptual_stmt_html,device_code,assembly,bitcode,static_library,c_header -o $(@D) -f generated_pipeline target=$(HL_TARGET)-no_runtime-no_bounds_query

$(BIN)/$(HL_TARGET)/test: $(BIN)/$(HL_TARGET)/generated_pipeline.a $(BIN)/$(HL_TARGET)/runtime.a test.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Wall -O2 -I$(BIN)/$(HL_TARGET) test.cpp $(BIN)/$(HL_TARGET)/generated_pipeline.a $(BIN)/$(HL_TARGET)/runtime.a -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN)

run: $(BIN)/$(HL_TARGET)/test
	$<


# Makefile inspired by the structure of the apps/

HALIDE_DISTRIB_PATH ?= ../../../distrib
include ../../../apps/support/Makefile.inc

.PHONY: build clean test codegen
build: $(BIN)/$(HL_TARGET)/generated_pipeline.stmt


$(GENERATOR_BIN)/generator: generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/generated_pipeline.stmt: $(GENERATOR_BIN)/generator
	@mkdir -p $(@D)
	$^ -g test_generator -e stmt,stmt_html,conceptual_stmt,conceptual_stmt_html,device_code,assembly,bitcode,static_library,c_header -o $(@D) -f generated_pipeline target=$*

$(BIN)/%/test: $(BIN)/%/generated_pipeline.a test.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Wall -O2 -I$(BIN)/$* test.cpp $(BIN)/$*/generated_pipeline.a -o $@ $(LDFLAGS)

test: $(BIN)/$(HL_TARGET)/test
	$<

clean:
	rm -rf $(BIN)

include ../support/Makefile.inc

.PHONY: build clean test

build: $(BIN)/$(HL_TARGET)/test

$(GENERATOR_BIN)/iir_cascade.generator: iir_cascade_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/iir_cascade.a: $(GENERATOR_BIN)/iir_cascade.generator
	@mkdir -p $(@D)
	$^ -g iir_cascade -f iir_cascade -e $(GENERATOR_OUTPUTS) -o $(@D) target=$*-no_runtime inductive=true

$(BIN)/%/iir_cascade_noninductive.a: $(GENERATOR_BIN)/iir_cascade.generator
	@mkdir -p $(@D)
	$^ -g iir_cascade -f iir_cascade_noninductive -e $(GENERATOR_OUTPUTS) -o $(@D) target=$*-no_runtime inductive=false

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/iir_cascade.generator
	@mkdir -p $(@D)
	$^ -r runtime -o $(@D) target=$*

$(BIN)/%/test: test.cpp $(BIN)/%/iir_cascade.a $(BIN)/%/iir_cascade_noninductive.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) -Wall -O2 -I$(BIN)/$* $^ -o $@ $(LDFLAGS-$*)

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/test
	$<

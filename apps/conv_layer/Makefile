include ../support/Makefile.inc

.PHONY: build clean test

build: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/conv_layer.generator: conv_layer_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/conv_layer.halide_generated.cpp: $(GENERATOR_BIN)/conv_layer.generator
	@mkdir -p $(@D)
	$^ -g conv_layer -o $(@D) -f conv_layer -e c_source,c_header target=$*-xtensa

$(BIN)/%/conv_layer.a: $(GENERATOR_BIN)/conv_layer.generator
	@mkdir -p $(@D)
	$^ -g conv_layer -e $(GENERATOR_OUTPUTS) -o $(@D) -f conv_layer target=$* auto_schedule=false

$(BIN)/%/conv_layer_auto_schedule.a: $(GENERATOR_BIN)/conv_layer.generator
	@mkdir -p $(@D)
	$^ -g conv_layer -e $(GENERATOR_OUTPUTS) -o $(@D) -f conv_layer_auto_schedule target=$*-no_runtime auto_schedule=true

$(BIN)/%/process: process.cpp $(BIN)/%/conv_layer.a $(BIN)/%/conv_layer_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS)

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/conv_layer.generator
	@mkdir -p $(@D)
	@$< -r runtime -o $(@D) target=$*

$(BIN)/%/process_xt_cstub: process.cpp $(BIN)/%/conv_layer.halide_generated.cpp $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DSKIP_BENCHMARK -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) -I${XTENSA_CSTUBS_ROOT} ${XTENSA_CSTUBS_ROOT}/libcstub.a

run: $(BIN)/$(HL_TARGET)/process
	@mkdir -p $(@D)
	$^

run_xt_cstub: $(BIN)/$(HL_TARGET)/process_xt_cstub
	@mkdir -p $(@D)
	$^

clean:
	rm -rf $(BIN)

test: run

.SECONDARY: $(BIN)/%/conv_layer.halide_generated.cpp
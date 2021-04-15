include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/depthwise_separable_conv.generator: depthwise_separable_conv_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/depthwise_separable_conv.a: $(GENERATOR_BIN)/depthwise_separable_conv.generator
	@mkdir -p $(@D)
	$^ -g depthwise_separable_conv -e $(GENERATOR_OUTPUTS) -o $(@D) -f depthwise_separable_conv target=$* auto_schedule=false

$(BIN)/%/depthwise_separable_conv_auto_schedule.a: $(GENERATOR_BIN)/depthwise_separable_conv.generator
	@mkdir -p $(@D)
	$^ -g depthwise_separable_conv -e $(GENERATOR_OUTPUTS) -o $(@D) -f depthwise_separable_conv_auto_schedule target=$*-no_runtime auto_schedule=true

$(BIN)/%/process: process.cpp $(BIN)/%/depthwise_separable_conv.a $(BIN)/%/depthwise_separable_conv_auto_schedule.a
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS)

test: $(BIN)/$(HL_TARGET)/process
	@mkdir -p $(@D)
	$^

clean:
	rm -rf $(BIN)


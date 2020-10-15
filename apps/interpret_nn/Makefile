include ../support/Makefile.inc

.PHONY: build clean test

# Note: this requires that you have flatbuffers installed locally.
# e.g. brew install flatbuffers

build: $(BIN)/$(HL_TARGET)/benchmark

test: $(BIN)/$(HL_TARGET)/benchmark
	$(BIN)/$(HL_TARGET)/benchmark

clean:
	rm -rf $(BIN)

CXXFLAGS += -Wno-unused-private-field

$(GENERATOR_BIN)/common_halide.o: halide/common_halide.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(GENERATOR_BIN)/%.generator: halide/%_generator.cpp $(GENERATOR_BIN)/common_halide.o $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp %.o,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/Add4DUint8Uint8.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	$< -g Add -f interpret_nn::Add4DUint8Uint8 -o $(BIN)/$* target=$*-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/ConvolutionUint8.a: $(GENERATOR_BIN)/convolution.generator
	@mkdir -p $(@D)
	$< -g Convolution -f interpret_nn::ConvolutionUint8 -o $(BIN)/$* target=$*-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/DepthwiseConvolutionUint8.a: $(GENERATOR_BIN)/depthwise_convolution.generator
	@mkdir -p $(@D)
	$< -g DepthwiseConvolution -f interpret_nn::DepthwiseConvolutionUint8 -o $(BIN)/$* target=$*-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

OPS_HALIDE = $(BIN)/%/Add4DUint8Uint8.a $(BIN)/%/ConvolutionUint8.a $(BIN)/%/DepthwiseConvolutionUint8.a $(BIN)/%/runtime.a

$(BIN)/%/interpret_nn.o: interpret_nn.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -c $< -o $@

$(BIN)/%/ops.o: ops.cpp $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -c $< -o $@

$(BIN)/%/tflite_parser.o: tflite_parser.cpp $(BIN)/%/tflite_schema_generated.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -c $< -o $@

$(BIN)/%/tflite_schema_generated.h:
	@echo Fetching tflite_schema_generated.h...
	@wget --quiet -O $@ https://github.com/tensorflow/tensorflow/raw/master/tensorflow/lite/schema/schema_generated.h

$(BIN)/%/benchmark: benchmark.cpp $(BIN)/%/interpret_nn.o $(BIN)/%/ops.o $(BIN)/%/tflite_parser.o $(BIN)/%/tflite_schema_generated.h $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS)


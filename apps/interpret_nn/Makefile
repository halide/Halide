include ../support/Makefile.inc

.PHONY: build clean test

# Note: this requires that you have flatbuffers installed locally.
# e.g. brew install flatbuffers

build: $(BIN)/$(HL_TARGET)/benchmark $(BIN)/$(HL_TARGET)/convolution_test $(BIN)/host/tflite_exploder

test: $(BIN)/$(HL_TARGET)/benchmark
	$(BIN)/$(HL_TARGET)/benchmark

clean:
	rm -rf $(BIN)

CXXFLAGS += -Wno-unused-private-field

$(GENERATOR_BIN)/common_halide.o: halide/common_halide.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PRECIOUS: $(GENERATOR_BIN)/%.generator
$(GENERATOR_BIN)/%.generator: halide/%_generator.cpp $(GENERATOR_BIN)/common_halide.o $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp %.o,$^) -o $@ $(LIBHALIDE_LDFLAGS)

.PRECIOUS: $(BIN)/%/AddUint8Uint8.a
$(BIN)/%/AddUint8Uint8.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	$< -g Add -f interpret_nn::AddUint8Uint8 -o $(BIN)/$* target=$*-no_runtime-c_plus_plus_name_mangling

.PRECIOUS: $(BIN)/%/AveragePoolUint8.a
$(BIN)/%/AveragePoolUint8.a: $(GENERATOR_BIN)/average_pool.generator
	@mkdir -p $(@D)
	$< -g AveragePool -f interpret_nn::AveragePoolUint8 -o $(BIN)/$* target=$*-no_runtime-c_plus_plus_name_mangling

.PRECIOUS: $(BIN)/%/ConvolutionUint8.a
$(BIN)/%/ConvolutionUint8.a: $(GENERATOR_BIN)/convolution.generator
	@mkdir -p $(@D)
	$< -g Convolution -f interpret_nn::ConvolutionUint8 -o $(BIN)/$* target=$*-no_runtime-c_plus_plus_name_mangling

.PRECIOUS: $(BIN)/%/DepthwiseConvolutionUint8.a
$(BIN)/%/DepthwiseConvolutionUint8.a: $(GENERATOR_BIN)/depthwise_convolution.generator
	@mkdir -p $(@D)
	$< -g DepthwiseConvolution -f interpret_nn::DepthwiseConvolutionUint8 -o $(BIN)/$* target=$*-no_runtime-c_plus_plus_name_mangling

.PRECIOUS: $(BIN)/%/MaxPoolUint8.a
$(BIN)/%/MaxPoolUint8.a: $(GENERATOR_BIN)/max_pool.generator
	@mkdir -p $(@D)
	$< -g MaxPool -f interpret_nn::MaxPoolUint8 -o $(BIN)/$* target=$*-no_runtime-c_plus_plus_name_mangling

.PRECIOUS: $(BIN)/%/runtime.a
$(BIN)/%/runtime.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

OPS_HALIDE = \
	$(BIN)/%/AddUint8Uint8.a \
	$(BIN)/%/AveragePoolUint8.a \
	$(BIN)/%/ConvolutionUint8.a \
	$(BIN)/%/DepthwiseConvolutionUint8.a \
	$(BIN)/%/MaxPoolUint8.a \
	$(BIN)/%/runtime.a

$(BIN)/%/interpreter.o: interpreter.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -c $< -o $@

$(BIN)/%/model.o: model.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -c $< -o $@

$(BIN)/%/ops.o: ops.cpp $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -c $< -o $@

$(BIN)/%/tflite_parser.o: tflite_parser.cpp $(BIN)/%/tflite_schema_generated.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -c $< -o $@

TFLITE_TAG ?= master

.PRECIOUS: $(BIN)/%/tflite_schema.fbs
$(BIN)/%/tflite_schema.fbs:
	@echo Fetching tflite_schema.fbs...
	@mkdir -p $(@D)
	@wget --quiet -O $@ https://github.com/tensorflow/tensorflow/raw/$(TFLITE_TAG)/tensorflow/lite/schema/schema.fbs

# This is a very minimal .h file that allows only for reading a flatbuffer...
# which is all tflite_parser needs.
.PRECIOUS: $(BIN)/%/tflite_schema_generated.h
$(BIN)/%/tflite_schema_generated.h: $(BIN)/%/tflite_schema.fbs
	@mkdir -p $(@D)
	flatc --cpp --no-includes -o $(BIN)/$* $<

# This include extra API needed only by utilities that do flatbuffer surgery (eg tflite_exploder)
.PRECIOUS: $(BIN)/%/tflite_schema_direct_generated.h
$(BIN)/%/tflite_schema_direct_generated.h: $(BIN)/%/tflite_schema.fbs
	@mkdir -p $(@D)
	flatc --cpp --no-includes --gen-object-api --filename-suffix _direct_generated -o $(BIN)/$* $<

$(BIN)/%/benchmark: benchmark.cpp $(BIN)/%/interpreter.o $(BIN)/%/model.o $(BIN)/%/tflite_parser.o $(BIN)/%/ops.o $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS)

$(BIN)/%/common_reference.o: halide/common_reference.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -c $< -o $@

$(BIN)/%/convolution_test: halide/convolution_test.cpp $(BIN)/%/common_reference.o $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS)

test_convolution: $(BIN)/$(HL_TARGET)/convolution_test
	$<

# Experimental glue code to define RunGen rules for all of these (from Makefile.inc)
# Not sure if useful or not, gonna leave in and see.
$(eval $(call DEFINE_RUNGEN,AddUint8Uint8,$(BIN)/%/runtime.a))
$(eval $(call DEFINE_RUNGEN,AveragePoolUint8,$(BIN)/%/runtime.a))
$(eval $(call DEFINE_RUNGEN,ConvolutionUint8,$(BIN)/%/runtime.a))
$(eval $(call DEFINE_RUNGEN,DepthwiseConvolutionUint8,$(BIN)/%/runtime.a))
$(eval $(call DEFINE_RUNGEN,MaxPoolUint8,$(BIN)/%/runtime.a))


$(BIN)/%/tflite_exploder: tflite_exploder.cpp $(BIN)/%/tflite_schema_direct_generated.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS)


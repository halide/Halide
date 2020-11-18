include ../support/Makefile.inc

.PHONY: build clean test

# Note: this requires that you have the flatbuffers `flatc` tool available on
# the host system; this is usually easy to install via e.g. brew install flatbuffers
FLATC ?= flatc

# It also requires the flatbuffers include files be in the C++ include path,
# but -- notably -- does NOT require libflatbuffers.a to be compiled for the
# target, so you don't need to crosscompile for (e.g.) Android. The includes
# will generally be installed by the same thing that installs flatc, but may
# not be in the default include path for the crosscompiler, so we need to
# explicitly point it correctly below:
#
# This is the location that brew installs on my Mac.
FLATBUFFERS_INCLUDE_PATH ?= /usr/local/Cellar/flatbuffers/1.12.0/include

build: $(BIN)/$(HL_TARGET)/benchmark \
	$(BIN)/$(HL_TARGET)/convolution_test \
	$(BIN)/$(HL_TARGET)/compare_vs_tflite \
	$(BIN)/host/tflite_exploder \


test: $(BIN)/$(HL_TARGET)/benchmark
	$(BIN)/$(HL_TARGET)/benchmark

clean:
	rm -rf $(BIN)

# Must clone and build locally for now:
# 	- git clone https://github.com/tensorflow/tensorflow
# 	- cd tensorflow
# 	- ./tensorflow/lite/tools/make/download_dependencies.sh
# 	- make -j $(nproc) TARGET=native -C . -f ./tensorflow/lite/tools/make/Makefile
# 	- library is at tensorflow/lite/tools/make/gen/native_x86_64/lib/libtensorflow-lite.a

TENSORFLOW_BASE ?= ${HOME}/GitHub/tensorflow
TFLITE_BASE ?= $(TENSORFLOW_BASE)/tensorflow/lite
TFLITE_STATIC_LIB ?= $(TFLITE_BASE)/tools/make/gen/native_x86_64/lib/libtensorflow-lite.a

TENSORFLOW_INCLUDES ?= $(TENSORFLOW_BASE)

CXXFLAGS += -Wno-unused-private-field

# .SECONDARY with no prerequisites causes all targets to be treated as secondary
# (i.e., no target is removed because it is considered intermediate).
.SECONDARY:

$(GENERATOR_BIN)/common_halide.o: halide/common_halide.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(GENERATOR_BIN)/%.generator: halide/%_generator.cpp $(GENERATOR_BIN)/common_halide.o $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp %.o,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/AddUint8Uint8.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	$< -g Add -f interpret_nn::AddUint8Uint8 -o $(BIN)/$* target=$*-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/AveragePoolUint8.a: $(GENERATOR_BIN)/average_pool.generator
	@mkdir -p $(@D)
	$< -g AveragePool -f interpret_nn::AveragePoolUint8 -o $(BIN)/$* target=$*-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/ConvolutionUint8.a: $(GENERATOR_BIN)/convolution.generator
	@mkdir -p $(@D)
	$< -g Convolution -f interpret_nn::ConvolutionUint8 -o $(BIN)/$* target=$*-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/DepthwiseConvolutionUint8.a: $(GENERATOR_BIN)/depthwise_convolution.generator
	@mkdir -p $(@D)
	$< -g DepthwiseConvolution -f interpret_nn::DepthwiseConvolutionUint8 -o $(BIN)/$* target=$*-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/MaxPoolUint8.a: $(GENERATOR_BIN)/max_pool.generator
	@mkdir -p $(@D)
	$< -g MaxPool -f interpret_nn::MaxPoolUint8 -o $(BIN)/$* target=$*-no_runtime-c_plus_plus_name_mangling

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
	$(CXX-$*) $(CXXFLAGS-$*) -I$(BIN)/$* -c $< -o $@

$(BIN)/%/model.o: model.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) -I$(BIN)/$* -c $< -o $@

$(BIN)/%/ops.o: ops.cpp $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) -I$(BIN)/$* -c $< -o $@

$(BIN)/%/interval.o: interval.cpp $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) -I$(BIN)/$* -c $< -o $@

$(BIN)/%/tflite_parser.o: tflite_parser.cpp $(BIN)/%/tflite_schema_generated.h
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) -I$(FLATBUFFERS_INCLUDE_PATH) -I$(BIN)/$* -c $< -o $@

TFLITE_TAG ?= master

$(BIN)/%/tflite_schema.fbs:
	@echo Fetching tflite_schema.fbs...
	@mkdir -p $(@D)
	@wget --quiet -O $@ https://github.com/tensorflow/tensorflow/raw/$(TFLITE_TAG)/tensorflow/lite/schema/schema.fbs

# This is a very minimal .h file that allows only for reading a flatbuffer...
# which is all tflite_parser needs.
$(BIN)/%/tflite_schema_generated.h: $(BIN)/%/tflite_schema.fbs
	@mkdir -p $(@D)
	$(FLATC) --cpp --no-includes -o $(BIN)/$* $<

# This include extra API needed only by utilities that do flatbuffer surgery (eg tflite_exploder)
$(BIN)/%/tflite_schema_direct_generated.h: $(BIN)/%/tflite_schema.fbs
	@mkdir -p $(@D)
	$(FLATC) --cpp --no-includes --gen-object-api --filename-suffix _direct_generated -o $(BIN)/$* $<

INTERPRETER_DEPS = \
	$(BIN)/%/interpreter.o \
	$(BIN)/%/interval.o \
	$(BIN)/%/model.o \
	$(BIN)/%/tflite_parser.o \
	$(BIN)/%/ops.o \
	$(OPS_HALIDE)

$(BIN)/%/benchmark: benchmark.cpp $(INTERPRETER_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) -I$(BIN)/$* $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/common_reference.o: halide/common_reference.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) -I$(BIN)/$* -c $< -o $@

$(BIN)/%/convolution_test: halide/convolution_test.cpp $(BIN)/%/common_reference.o $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) -I$(BIN)/$* $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

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


$(BIN)/%/compare_vs_tflite: compare_vs_tflite.cpp $(INTERPRETER_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) -I$(BIN)/$* -I$(TENSORFLOW_INCLUDES) $(filter %.cpp %.o %.a,$^) $(TFLITE_STATIC_LIB) -o $@ $(LDFLAGS-$*)


include ../support/Makefile.inc

# Note: this requires that you have the flatbuffers `flatc` tool available on
# the host system; this is usually easy to install via e.g.
#
#     apt-get install libflatbuffers-dev
#     brew install flatbuffers
#
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

CXXFLAGS += -Wno-unused-private-field

MAKEFILE_DIR=$(dir $(realpath $(firstword $(MAKEFILE_LIST))))

.PHONY: all build clean test build_test_ops
all: build

# .SECONDARY with no prerequisites causes all targets to be treated as secondary
# (i.e., no target is removed because it is considered intermediate).
.SECONDARY:

build: \
	$(BIN)/$(HL_TARGET)/benchmark \
	$(BIN)/$(HL_TARGET)/compare_vs_tflite \
	$(BIN)/host/tflite_exploder \
	build_test_ops

test: test_ops

clean:
	rm -rf $(BIN)

APP_CXXFLAGS = -I$(MAKEFILE_DIR)

# ---------------------- halide

$(GENERATOR_BIN)/common_halide.o: halide/common_halide.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(GENERATOR_BIN)/%.generator: halide/%_generator.cpp $(GENERATOR_BIN)/common_halide.o $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp %.o,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/add_uint8_uint8.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	$< -g Add -f interpret_nn::add_uint8_uint8 -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/average_pool_uint8.a: $(GENERATOR_BIN)/average_pool.generator
	@mkdir -p $(@D)
	$< -g AveragePool -f interpret_nn::average_pool_uint8 -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/convolution_uint8.a: $(GENERATOR_BIN)/convolution.generator
	@mkdir -p $(@D)
	$< -g Convolution -f interpret_nn::convolution_uint8 -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/depthwise_convolution_uint8.a: $(GENERATOR_BIN)/depthwise_convolution.generator
	@mkdir -p $(@D)
	$< -g DepthwiseConvolution -f interpret_nn::depthwise_convolution_uint8 -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/depthwise_convolution_uint8_broadcast.a: $(GENERATOR_BIN)/depthwise_convolution.generator
	@mkdir -p $(@D)
	$< -g DepthwiseConvolution broadcast_channels=true -f interpret_nn::depthwise_convolution_uint8_broadcast -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/fully_connected_uint8.a: $(GENERATOR_BIN)/fully_connected.generator
	@mkdir -p $(@D)
	$< -g FullyConnected -f interpret_nn::fully_connected_uint8 -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/max_pool_uint8.a: $(GENERATOR_BIN)/max_pool.generator
	@mkdir -p $(@D)
	$< -g MaxPool -f interpret_nn::max_pool_uint8 -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$(HL_TARGET)

OPS_HALIDE = \
	$(BIN)/%/add_uint8_uint8.a \
	$(BIN)/%/average_pool_uint8.a \
	$(BIN)/%/convolution_uint8.a \
	$(BIN)/%/depthwise_convolution_uint8.a \
	$(BIN)/%/depthwise_convolution_uint8_broadcast.a \
	$(BIN)/%/fully_connected_uint8.a \
	$(BIN)/%/max_pool_uint8.a \
	$(BIN)/%/runtime.a

OPS_CXXFLAGS = -I$(BIN)/$*

# ---------------------- util

# (empty)

# ---------------------- interpreter

$(BIN)/%/interpreter.o: interpreter/interpreter.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -c $< -o $@

$(BIN)/%/model.o: interpreter/model.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -c $< -o $@

$(BIN)/%/ops.o: interpreter/ops.cpp $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(OPS_CXXFLAGS) -c $< -o $@

$(BIN)/%/interval.o: interpreter/interval.cpp $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -c $< -o $@

INTERPRETER_DEPS = \
	$(BIN)/%/interpreter.o \
	$(BIN)/%/interval.o \
	$(BIN)/%/model.o \
	$(BIN)/%/ops.o \
	$(OPS_HALIDE)

# ---------------------- tflite

# TODO: switch to v2.4.0 once released
TFLITE_TAG ?= v2.4.0-rc3

$(BIN)/schema/tflite_schema.fbs:
	@echo Fetching tflite_schema.fbs...
	@mkdir -p $(@D)
	@wget --quiet -O $@ https://github.com/tensorflow/tensorflow/raw/$(TFLITE_TAG)/tensorflow/lite/schema/schema.fbs

# This is a very minimal .h file that allows only for reading a flatbuffer...
# which is all tflite_parser needs.
$(BIN)/schema/tflite_schema_generated.h: $(BIN)/schema/tflite_schema.fbs
	@mkdir -p $(@D)
	$(FLATC) --cpp --no-includes -o $(@D) $<

# This include extra API needed only by utilities that do flatbuffer surgery (eg tflite_exploder)
$(BIN)/schema/tflite_schema_direct_generated.h: $(BIN)/schema/tflite_schema.fbs
	@mkdir -p $(@D)
	$(FLATC) --cpp --no-includes --gen-object-api --filename-suffix _direct_generated -o $(@D) $<

TFLITE_SCHEMA_CXXFLAGS = -I$(BIN)/schema

$(BIN)/%/tflite_parser.o: tflite/tflite_parser.cpp $(BIN)/schema/tflite_schema_generated.h
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -I$(FLATBUFFERS_INCLUDE_PATH) $(TFLITE_SCHEMA_CXXFLAGS) -c $< -o $@

$(BIN)/%/tflite_parser.o: tflite/tflite_parser.cpp $(BIN)/schema/tflite_schema_generated.h
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -I$(FLATBUFFERS_INCLUDE_PATH) $(TFLITE_SCHEMA_CXXFLAGS) -c $< -o $@

TFLITE_PARSER_DEPS = \
	$(BIN)/%/tflite_parser.o

# ---------------------- test

# unit tests for each op (not yet complete)
$(BIN)/%/add_test: test/add_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/average_pool_test: test/average_pool_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/concatenation_test: test/concatenation_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/conv2d_test: test/conv2d_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/depthwise_conv2d_test: test/depthwise_conv2d_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/fully_connected_test: test/fully_connected_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/max_pool_test: test/max_pool_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/pad_test: test/pad_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/quantize_test: test/quantize_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/reshape_test: test/reshape_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

build_test_ops: \
	$(BIN)/$(HL_TARGET)/add_test \
	$(BIN)/$(HL_TARGET)/average_pool_test \
	$(BIN)/$(HL_TARGET)/concatenation_test \
	$(BIN)/$(HL_TARGET)/conv2d_test \
	$(BIN)/$(HL_TARGET)/depthwise_conv2d_test \
	$(BIN)/$(HL_TARGET)/fully_connected_test \
	$(BIN)/$(HL_TARGET)/max_pool_test \
	$(BIN)/$(HL_TARGET)/pad_test \
	$(BIN)/$(HL_TARGET)/quantize_test \
	$(BIN)/$(HL_TARGET)/reshape_test

test_ops: build_test_ops
	$(BIN)/$(HL_TARGET)/add_test
	$(BIN)/$(HL_TARGET)/average_pool_test
	$(BIN)/$(HL_TARGET)/concatenation_test
	$(BIN)/$(HL_TARGET)/conv2d_test
	$(BIN)/$(HL_TARGET)/depthwise_conv2d_test
	$(BIN)/$(HL_TARGET)/fully_connected_test
	$(BIN)/$(HL_TARGET)/max_pool_test
	$(BIN)/$(HL_TARGET)/pad_test
	$(BIN)/$(HL_TARGET)/quantize_test
	$(BIN)/$(HL_TARGET)/reshape_test

# ---------------------- toplevel executables

$(BIN)/%/benchmark: benchmark.cpp $(INTERPRETER_DEPS) $(TFLITE_PARSER_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

# Utility to explode a tflite pipeline into individual ops for testing.
$(BIN)/%/tflite_exploder: tflite_exploder.cpp $(BIN)/schema/tflite_schema_direct_generated.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(APP_CXXFLAGS) $(TFLITE_SCHEMA_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS)

# compare_vs_tflite requires the tflite includes and static library.
#
# For now, you must clone and build tflite locally if you want to build compare_vs_tflite:
#
# 	- git clone https://github.com/tensorflow/tensorflow
# 	- cd tensorflow
#   - git checkout $(TFLITE_TAG)
# 	- ./tensorflow/lite/tools/make/download_dependencies.sh
# 	- make -j $(nproc) TARGET=native -C . -f ./tensorflow/lite/tools/make/Makefile
# 	- library is at tensorflow/lite/tools/make/gen/native_x86_64/lib/libtensorflow-lite.a

TENSORFLOW_BASE ?= ${HOME}/GitHub/tensorflow
TFLITE_BASE ?= $(TENSORFLOW_BASE)/tensorflow/lite
TFLITE_STATIC_LIB ?= $(TFLITE_BASE)/tools/make/gen/native_x86_64/lib/libtensorflow-lite.a

TENSORFLOW_INCLUDES ?= $(TENSORFLOW_BASE)

$(BIN)/%/compare_vs_tflite: compare_vs_tflite.cpp $(INTERPRETER_DEPS)  $(TFLITE_PARSER_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -I$(TENSORFLOW_INCLUDES) $(filter %.cpp %.o %.a,$^) $(TFLITE_STATIC_LIB) -o $@ $(LDFLAGS-$*)


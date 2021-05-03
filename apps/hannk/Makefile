include ../support/Makefile.inc

# Removing exceptions just because we don't need 'em and it saves space.
#
# -fPIC is necessary for .so builds (at least on Linux); not necessary for the non-delegate
# builds but easier to enable it for everything.
CXXFLAGS += -Wno-unused-private-field -fno-exceptions -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -Wunused-variable -I$(MAKEFILE_DIR)

ifneq (,$(findstring -O,$(OPTIMIZE)))
	CXXFLAGS += -DNDEBUG
endif

MAKEFILE_DIR=$(dir $(realpath $(firstword $(MAKEFILE_LIST))))

.PHONY: all build clean test delegate
all: build

# .SECONDARY with no prerequisites causes all targets to be treated as secondary
# (i.e., no target is removed because it is considered intermediate).
.SECONDARY:

build: \
	$(BIN)/$(HL_TARGET)/benchmark \
	$(BIN)/$(HL_TARGET)/compare_vs_tflite

test: compare_vs_tflite
	$(BIN)/$(HL_TARGET)/compare_vs_tflite test/*/* --benchmark 0

clean:
	rm -rf $(BIN)

# ---------------------- TFLite glue

TFLITE_VERSION_MAJOR ?= 2
TFLITE_VERSION_MINOR ?= 5
TFLITE_VERSION_PATCH ?= 0-rc2

TFLITE_VERSION = $(TFLITE_VERSION_MAJOR).$(TFLITE_VERSION_MINOR).$(TFLITE_VERSION_PATCH)
TFLITE_TAG = v$(TFLITE_VERSION)

# Note that we require the C API for TFLite (not the C++ API).
# Normally, TENSORFLOW_BASE is the only symbol you need to define.
# (This seems like a plausible default.)
#
# Sample steps to build for desktop (see also https://www.tensorflow.org/lite/guide/build_cmake):
#
#   $ git clone https://github.com/tensorflow/tensorflow
#   $ cd tensorflow
#   $ git checkout $(TFLITE_TAG)
#   $ mkdir tflite_build && cd tflite_build
#   $ cmake ../tensorflow/lite/c
#   $ cmake --build . -j`nproc`
#
# For Android, you must have the Android NDK installed locally, with ANDROID_NDK_ROOT
# defined to point to it. Sample steps for Android:
#
#   $ mkdir tflite_build_android && cd tflite_build_android
#   $ cmake ../tensorflow/lite/c \
#         -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a
#   $ cmake --build . -j`nproc`

# Assume a desktop system for now (linux, osx, etc)
TENSORFLOW_BASE ?= $(HOME)/GitHub/tensorflow
TFLITE_INCLUDES ?= $(TENSORFLOW_BASE)

ifeq (arm-64-android,$(findstring arm-64-android,$(HL_TARGET)))

TFLITE_SHARED_LIBRARY ?= $(TENSORFLOW_BASE)/tflite_build_android/libtensorflowlite_c.so
# Set the rpath to . on Android since run_compare_on_device.sh will push the .so to the same dir as the app
TFLITE_LDFLAGS=-Wl,-rpath,.

else

TFLITE_SHARED_LIBRARY ?= $(TENSORFLOW_BASE)/tflite_build/libtensorflowlite_c.$(SHARED_EXT)
# Point at the TFLite path, but also the exe directory (for libHannkDelegate.so)
TFLITE_LDFLAGS = -Wl,-rpath,$(dir $(TFLITE_SHARED_LIBRARY)),-rpath,$(dir $@)

endif

FLATBUFFER_INCLUDES ?= $(dir $(TFLITE_SHARED_LIBRARY))/flatbuffers/include

# ---------------------- misc

# Define `TFLITE_VERSION` here to allow for code that compiles against multiple versions of
# TFLite (both the C API and the Schema).
APP_CXXFLAGS = -I$(MAKEFILE_DIR) \
	-DTFLITE_VERSION_MAJOR=$(TFLITE_VERSION_MAJOR) \
	-DTFLITE_VERSION_MINOR=$(TFLITE_VERSION_MINOR) \
	-DTFLITE_VERSION_PATCH=$(TFLITE_VERSION_PATCH)


# ---------------------- halide

$(GENERATOR_BIN)/common_halide.o: halide/common_halide.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(GENERATOR_BIN)/common_halide_test: halide/common_halide_test.cpp $(GENERATOR_BIN)/common_halide.o
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp %.o,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(GENERATOR_BIN)/%.generator: halide/%_generator.cpp $(GENERATOR_BIN)/common_halide.o $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp %.o,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/halide/add_uint8_uint8.a: $(GENERATOR_BIN)/elementwise.generator
	@mkdir -p $(@D)
	$< -g Add -f hannk::add_uint8_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/average_pool_uint8.a: $(GENERATOR_BIN)/pool.generator
	@mkdir -p $(@D)
	$< -g AveragePool -f hannk::average_pool_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/conv_uint8.a: $(GENERATOR_BIN)/conv.generator
	@mkdir -p $(@D)
	$< -g Conv -f hannk::conv_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/conv_r16_uint8.a: $(GENERATOR_BIN)/conv.generator
	@mkdir -p $(@D)
	$< -g Conv unroll_reduction=16 -f hannk::conv_r16_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/copy_uint8_uint8.a: $(GENERATOR_BIN)/copy.generator
	@mkdir -p $(@D)
	$< -g Copy input.type=uint8 output.type=uint8 -f hannk::copy_uint8_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_bounds_query-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/depthwise_conv_uint8.a: $(GENERATOR_BIN)/depthwise_conv.generator
	@mkdir -p $(@D)
	$< -g DepthwiseConv -f hannk::depthwise_conv_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/depthwise_conv_broadcast_uint8.a: $(GENERATOR_BIN)/depthwise_conv.generator
	@mkdir -p $(@D)
	$< -g DepthwiseConv inv_depth_multiplier=0 -f hannk::depthwise_conv_broadcast_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/depthwise_conv_dm1_uint8.a: $(GENERATOR_BIN)/depthwise_conv.generator
	@mkdir -p $(@D)
	$< -g DepthwiseConv inv_depth_multiplier=1 -f hannk::depthwise_conv_dm1_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/elementwise_5xuint8_1xuint8.a: $(GENERATOR_BIN)/elementwise.generator
	@mkdir -p $(@D)
	$< -g Elementwise inputs.size=5 inputs.type=uint8 output1_type=uint8 -f hannk::elementwise_5xuint8_1xuint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/elementwise_5xint16_1xuint8int16.a: $(GENERATOR_BIN)/elementwise.generator
	@mkdir -p $(@D)
	$< -g Elementwise inputs.size=5 inputs.type=int16 output1_type=uint8 output2_type=int16 -f hannk::elementwise_5xint16_1xuint8int16 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/fill_uint8.a: $(GENERATOR_BIN)/fill.generator
	@mkdir -p $(@D)
	$< -g Fill -f hannk::fill_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_bounds_query-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/fully_connected_uint8_uint8.a: $(GENERATOR_BIN)/fully_connected.generator
	@mkdir -p $(@D)
	$< -g FullyConnected output.type=uint8 -f hannk::fully_connected_uint8_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/fully_connected_uint8_int16.a: $(GENERATOR_BIN)/fully_connected.generator
	@mkdir -p $(@D)
	$< -g FullyConnected output.type=int16 -f hannk::fully_connected_uint8_int16 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/l2_normalization_uint8.a: $(GENERATOR_BIN)/normalizations.generator
	@mkdir -p $(@D)
	$< -g L2Normalization -f hannk::l2_normalization_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/max_pool_uint8.a: $(GENERATOR_BIN)/pool.generator
	@mkdir -p $(@D)
	$< -g MaxPool -f hannk::max_pool_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/mean_uint8.a: $(GENERATOR_BIN)/reductions.generator
	@mkdir -p $(@D)
	$< -g Mean -f hannk::mean_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/mul_uint8_uint8_uint8.a: $(GENERATOR_BIN)/elementwise.generator
	@mkdir -p $(@D)
	$< -g Mul -f hannk::mul_uint8_uint8_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/softmax_uint8.a: $(GENERATOR_BIN)/normalizations.generator
	@mkdir -p $(@D)
	$< -g Softmax -f hannk::softmax_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/tile_conv_filter_uint8.a: $(GENERATOR_BIN)/conv.generator
	@mkdir -p $(@D)
	$< -g TileConvFilter -f hannk::tile_conv_filter_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-large_buffers-c_plus_plus_name_mangling -e static_library,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/runtime.a: $(GENERATOR_BIN)/fill.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$*/halide target=$(HL_TARGET)

OPS_HALIDE = \
	$(BIN)/%/halide/add_uint8_uint8.a \
	$(BIN)/%/halide/average_pool_uint8.a \
	$(BIN)/%/halide/conv_uint8.a \
	$(BIN)/%/halide/copy_uint8_uint8.a \
	$(BIN)/%/halide/depthwise_conv_uint8.a \
	$(BIN)/%/halide/depthwise_conv_broadcast_uint8.a \
	$(BIN)/%/halide/depthwise_conv_dm1_uint8.a \
	$(BIN)/%/halide/elementwise_5xuint8_1xuint8.a \
	$(BIN)/%/halide/elementwise_5xint16_1xuint8int16.a \
	$(BIN)/%/halide/fill_uint8.a \
	$(BIN)/%/halide/fully_connected_uint8_uint8.a \
	$(BIN)/%/halide/fully_connected_uint8_int16.a \
	$(BIN)/%/halide/l2_normalization_uint8.a \
	$(BIN)/%/halide/max_pool_uint8.a \
	$(BIN)/%/halide/mean_uint8.a \
	$(BIN)/%/halide/mul_uint8_uint8_uint8.a \
	$(BIN)/%/halide/softmax_uint8.a \
	$(BIN)/%/halide/tile_conv_filter_uint8.a \
	$(BIN)/%/halide/runtime.a

OPS_CXXFLAGS = -I$(BIN)/$*

ifneq (,$(findstring arm_dot_prod,$(HL_TARGET)))
OPS_HALIDE += $(BIN)/%/halide/conv_r16_uint8.a
OPS_CXXFLAGS += -DCONV_R16
endif

# ---------------------- util

$(BIN)/%/error_util.o: util/error_util.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -c $< -o $@

$(BIN)/%/hannk_log_stderr.o: util/hannk_log_stderr.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -c $< -o $@

UTIL_DEPS = \
	$(BIN)/%/error_util.o \
	$(BIN)/%/hannk_log_stderr.o

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

$(BIN)/%/elementwise_program.o: interpreter/elementwise_program.cpp $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(OPS_CXXFLAGS) -c $< -o $@

$(BIN)/%/lower.o: interpreter/lower.cpp $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(OPS_CXXFLAGS) -c $< -o $@

$(BIN)/%/transforms.o: interpreter/transforms.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -c $< -o $@

$(BIN)/%/interval.o: interpreter/interval.cpp $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -c $< -o $@

INTERPRETER_DEPS = \
	$(BIN)/%/interpreter.o \
	$(BIN)/%/interval.o \
	$(BIN)/%/lower.o \
	$(BIN)/%/elementwise_program.o \
	$(BIN)/%/model.o \
	$(BIN)/%/transforms.o \
	$(BIN)/%/ops.o \
	$(OPS_HALIDE)

# ---------------------- tflite-parser

TFLITE_SCHEMA_CXXFLAGS = -I$(TFLITE_INCLUDES) -I$(FLATBUFFER_INCLUDES)

$(BIN)/%/tflite_parser.o: tflite/tflite_parser.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(TFLITE_SCHEMA_CXXFLAGS) -c $< -o $@

TFLITE_PARSER_DEPS = \
	$(BIN)/%/tflite_parser.o

# ---------------------- delegate

TFLITE_INCLUDES_FLAGS ?= -I$(HOME)/GitHub/tensorflow -I$(MAKEFILE_DIR)

$(BIN)/%/hannk_delegate.o: delegate/hannk_delegate.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(TFLITE_INCLUDES_FLAGS) $(UTIL_CXXFLAGS) -c $< -o $@

$(BIN)/%/hannk_delegate_adaptor.o: delegate/hannk_delegate_adaptor.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(TFLITE_INCLUDES_FLAGS) $(UTIL_CXXFLAGS) -c $< -o $@

$(BIN)/%/hannk_delegate_provider.o: delegate/hannk_delegate_provider.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(TFLITE_INCLUDES_FLAGS) $(UTIL_CXXFLAGS) -c $< -o $@

ifeq (arm-64-android,$(findstring arm-64-android,$(HL_TARGET)))

# Don't include $(LDFLAGS) here, it includes pthreads, which we don't want
DELEGATE_LD_FLAGS=-static-libstdc++
DELEGATE_LD_FLAGS += -Wl,--version-script=delegate/exported_symbols.ldscript
DELEGATE_LD_FLAGS += -Wl,-soname,libHannkDelegate.so

else

DELEGATE_LD_FLAGS=$(LDFLAGS)

ifeq ($(UNAME), Darwin)
DELEGATE_LD_FLAGS += -Wl,-exported_symbols_list delegate/exported_symbols.osx
else
# Assume Desktop Linux
DELEGATE_LD_FLAGS += -Wl,--version-script=delegate/exported_symbols.ldscript
DELEGATE_LD_FLAGS += -Wl,-soname,libHannkDelegate.so
endif

endif

# Note: delegates are apparently always .so on OSX (never .dylib)
$(BIN)/%/libHannkDelegate.so: \
		$(BIN)/%/hannk_delegate.o \
		$(BIN)/%/hannk_delegate_adaptor.o \
		$(INTERPRETER_DEPS) \
		$(UTIL_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) -shared $^ $(DELEGATE_LD_FLAGS) -o $@

$(BIN)/%/hannk_delegate_provider.a: $(BIN)/%/hannk_delegate.o $(BIN)/%/hannk_delegate_provider.o
	@mkdir -p $(@D)
	ar q $@ $(OBJECTS) $^
	ranlib $@

# ---------------------- toplevel executables

$(BIN)/%/benchmark: benchmark.cpp $(INTERPRETER_DEPS) $(TFLITE_PARSER_DEPS) $(UTIL_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)



# To build for Android, use `HL_TARGET=arm-64-android make compare_vs_tflite`
$(BIN)/%/compare_vs_tflite: compare_vs_tflite.cpp \
		$(INTERPRETER_DEPS) $(TFLITE_PARSER_DEPS) $(UTIL_DEPS) $(TFLITE_SHARED_LIBRARY) $(BIN)/%/hannk_delegate_provider.a $(BIN)/$(HL_TARGET)/libHannkDelegate.so
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -I$(TFLITE_INCLUDES) $(TFLITE_LDFLAGS) $(filter %.cpp %.o %.a %.so %.$(SHARED_EXT),$^) -o $@ $(LDFLAGS-$*)

.PHONY: compare_vs_tflite

compare_vs_tflite: $(BIN)/$(HL_TARGET)/compare_vs_tflite


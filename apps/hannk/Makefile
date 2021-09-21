include ../support/Makefile.inc

# Removing exceptions just because we don't need 'em and it saves space.
#
# -fPIC is necessary for .so builds (at least on Linux); not necessary for the non-delegate
# builds but easier to enable it for everything.
CXXFLAGS += -Wno-unused-private-field -fno-exceptions -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -Wunused-variable -I$(MAKEFILE_DIR)

BENCHMARK_OUT = benchmark
ifeq (hexagon-32-qurt,$(findstring hexagon-32-qurt,$(HL_TARGET)))
	# Building benchmark application as shared object instead of elf for
	# hexagon target.
	BENCHMARK_OUT := $(BENCHMARK_OUT).so
	BENCHMARK_HEXAGON_FLAGS = -shared -fPIC -G0
	HEXAGON_STUBS = $(BIN)/$(HL_TARGET)/stubs.o
endif


ifneq (,$(findstring -O,$(OPTIMIZE)))
	CXXFLAGS += -DNDEBUG
endif

MAKEFILE_DIR=$(dir $(realpath $(firstword $(MAKEFILE_LIST))))

.PHONY: all build clean test delegate test-hexagon-sim test-hexagon-device
all: build

# .SECONDARY with no prerequisites causes all targets to be treated as secondary
# (i.e., no target is removed because it is considered intermediate).
.SECONDARY:

build: \
	$(BIN)/$(HL_TARGET)/$(BENCHMARK_OUT) \
	$(BIN)/$(HL_TARGET)/compare_vs_tflite

test: compare_vs_tflite
	$(foreach test_model, $(shell ls -1 test/*/*), $(BIN)/$(HL_TARGET)/compare_vs_tflite $(test_model) --benchmark 0;)

test-hexagon-sim: $(BIN)/$(HL_TARGET)/$(BENCHMARK_OUT)
	@mkdir -p $@
	echo "Benchmarking tests..."
	$(foreach test_model, $(shell ls -1 test/*/*), BIN=$(BIN) ./run_benchmark_on_hexagon_sim.sh $(test_model);)

test-hexagon-device: $(BIN)/$(HL_TARGET)/$(BENCHMARK_OUT)
	echo "Benchmarking tests..."
	adb push $< /vendor/bin/$(BENCHMARK_OUT)
	adb shell mkdir -p /vendor/bin/hannk-test
	adb push test/. /vendor/bin/hannk-test
	adb push $(HEXAGON_SDK_ROOT)/libs/run_main_on_hexagon/ship/android_aarch64/run_main_on_hexagon /vendor/bin/run_main_on_hexagon
	adb push $(HEXAGON_SDK_ROOT)/libs/run_main_on_hexagon/ship/hexagon_toolv84_v65/librun_main_on_hexagon_skel.so /vendor/lib/rfsa/adsp/
	$(foreach test_model, $(shell ls -1 test/*/*), adb shell 'cd /vendor/bin; touch /vendor/lib/rfsa/adsp/run_main_on_hexagon.farf; ./run_main_on_hexagon 3 /vendor/bin/$(BENCHMARK_OUT) --verbose hannk-$(test_model)';)

clean:
	rm -rf $(BIN)

# ---------------------- TFLite glue

TFLITE_VERSION_MAJOR ?= 2
TFLITE_VERSION_MINOR ?= 6
TFLITE_VERSION_PATCH ?= 0

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
#         -DANDROID_ABI=arm64-v8a \
#         -DCMAKE_Fortran_COMPILER=NO
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

$(BIN)/%/halide/add_uint8_uint8.o: $(GENERATOR_BIN)/elementwise.generator
	@mkdir -p $(@D)
	$< -g Add -f hannk::add_uint8_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-no_bounds_query-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/average_pool_uint8.o: $(GENERATOR_BIN)/pool.generator
	@mkdir -p $(@D)
	$< -g AveragePool -f hannk::average_pool_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/conv_u8_u8_u8.o: $(GENERATOR_BIN)/conv.generator
	@mkdir -p $(@D)
	$< -g Conv output.type=uint8 -f hannk::conv_u8_u8_u8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/conv_u8_u8_i16.o: $(GENERATOR_BIN)/conv.generator
	@mkdir -p $(@D)
	$< -g Conv output.type=int16 -f hannk::conv_u8_u8_i16 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/conv_r16_u8_u8_u8.o: $(GENERATOR_BIN)/conv.generator
	@mkdir -p $(@D)
	$< -g Conv unroll_reduction=16 output.type=uint8  -f hannk::conv_r16_u8_u8_u8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/conv_r16_u8_u8_i16.o: $(GENERATOR_BIN)/conv.generator
	@mkdir -p $(@D)
	$< -g Conv unroll_reduction=16 output.type=int16  -f hannk::conv_r16_u8_u8_i16 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/copy_uint8_uint8.o: $(GENERATOR_BIN)/copy.generator
	@mkdir -p $(@D)
	$< -g Copy input.type=uint8 output.type=uint8 -f hannk::copy_uint8_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-no_bounds_query-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/depthwise_conv_broadcast_uint8.o: $(GENERATOR_BIN)/depthwise_conv.generator
	@mkdir -p $(@D)
	$< -g DepthwiseConv inv_depth_multiplier=0 -f hannk::depthwise_conv_broadcast_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/depthwise_conv_uint8.o: $(GENERATOR_BIN)/depthwise_conv.generator
	@mkdir -p $(@D)
	$< -g DepthwiseConv inv_depth_multiplier=1 -f hannk::depthwise_conv_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/depthwise_conv_shallow_uint8.o: $(GENERATOR_BIN)/depthwise_conv.generator
	@mkdir -p $(@D)
	$< -g DepthwiseConv inv_depth_multiplier=1 shallow=true -f hannk::depthwise_conv_shallow_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/elementwise_5xuint8_1xuint8.o: $(GENERATOR_BIN)/elementwise.generator
	@mkdir -p $(@D)
	$< -g Elementwise inputs.size=5 inputs.type=uint8 output1_type=uint8 -f hannk::elementwise_5xuint8_1xuint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-no_bounds_query-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/elementwise_5xint16_1xuint8int16.o: $(GENERATOR_BIN)/elementwise.generator
	@mkdir -p $(@D)
	$< -g Elementwise inputs.size=5 inputs.type=int16 output1_type=uint8 output2_type=int16 -f hannk::elementwise_5xint16_1xuint8int16 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-no_bounds_query-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/fill_uint8.o: $(GENERATOR_BIN)/fill.generator
	@mkdir -p $(@D)
	$< -g Fill -f hannk::fill_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-no_asserts-no_bounds_query-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/l2_normalization_uint8.o: $(GENERATOR_BIN)/normalizations.generator
	@mkdir -p $(@D)
	$< -g L2Normalization -f hannk::l2_normalization_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-no_bounds_query-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/max_pool_uint8.o: $(GENERATOR_BIN)/pool.generator
	@mkdir -p $(@D)
	$< -g MaxPool -f hannk::max_pool_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/mean_uint8.o: $(GENERATOR_BIN)/reductions.generator
	@mkdir -p $(@D)
	$< -g Mean -f hannk::mean_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/mul_uint8_uint8_uint8.o: $(GENERATOR_BIN)/elementwise.generator
	@mkdir -p $(@D)
	$< -g Mul -f hannk::mul_uint8_uint8_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-no_bounds_query-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/softmax_uint8.o: $(GENERATOR_BIN)/normalizations.generator
	@mkdir -p $(@D)
	$< -g Softmax -f hannk::softmax_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-no_bounds_query-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/tile_conv_filter_uint8.o: $(GENERATOR_BIN)/conv.generator
	@mkdir -p $(@D)
	$< -g TileConvFilter -f hannk::tile_conv_filter_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/upsample_channels_uint8.o: $(GENERATOR_BIN)/depthwise_conv.generator
	@mkdir -p $(@D)
	$< -g UpsampleChannels -f hannk::upsample_channels_uint8 -o $(BIN)/$*/halide target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling -e object,assembly,stmt,c_header,llvm_assembly

$(BIN)/%/halide/runtime.o: $(GENERATOR_BIN)/fill.generator
	@mkdir -p $(@D)
	$< -r runtime -e object -o $(BIN)/$*/halide target=$(HL_TARGET)

OPS_HALIDE = \
	$(BIN)/%/halide/add_uint8_uint8.o \
	$(BIN)/%/halide/average_pool_uint8.o \
	$(BIN)/%/halide/conv_u8_u8_u8.o \
	$(BIN)/%/halide/conv_u8_u8_i16.o \
	$(BIN)/%/halide/copy_uint8_uint8.o \
	$(BIN)/%/halide/depthwise_conv_uint8.o \
	$(BIN)/%/halide/depthwise_conv_broadcast_uint8.o \
	$(BIN)/%/halide/depthwise_conv_shallow_uint8.o \
	$(BIN)/%/halide/elementwise_5xuint8_1xuint8.o \
	$(BIN)/%/halide/elementwise_5xint16_1xuint8int16.o \
	$(BIN)/%/halide/fill_uint8.o \
	$(BIN)/%/halide/l2_normalization_uint8.o \
	$(BIN)/%/halide/max_pool_uint8.o \
	$(BIN)/%/halide/mean_uint8.o \
	$(BIN)/%/halide/mul_uint8_uint8_uint8.o \
	$(BIN)/%/halide/softmax_uint8.o \
	$(BIN)/%/halide/tile_conv_filter_uint8.o \
	$(BIN)/%/halide/upsample_channels_uint8.o \
	$(BIN)/%/halide/runtime.o

OPS_CXXFLAGS = -I$(BIN)/$*

ifneq (,$(findstring arm_dot_prod,$(HL_TARGET)))
OPS_HALIDE += $(BIN)/%/halide/conv_r16_u8_u8_u8.o
OPS_HALIDE += $(BIN)/%/halide/conv_r16_u8_u8_i16.o
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

$(BIN)/%/tensor.o: interpreter/tensor.cpp
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

$(BIN)/%/allocation_planner.o: interpreter/allocation_planner.cpp $(OPS_HALIDE)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -c $< -o $@

# Only needed for hexagon target.
$(BIN)/%/stubs.o: interpreter/stubs.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(OPS_CXXFLAGS) -c $< -o $@

INTERPRETER_DEPS = \
	$(BIN)/%/interpreter.o \
	$(BIN)/%/interval.o \
	$(BIN)/%/lower.o \
	$(BIN)/%/elementwise_program.o \
	$(BIN)/%/model.o \
	$(BIN)/%/tensor.o \
	$(BIN)/%/transforms.o \
	$(BIN)/%/ops.o \
	$(BIN)/%/allocation_planner.o \
	$(OPS_HALIDE) $(HEXAGON_STUBS)

# ---------------------- libHANNK

$(GENERATOR_BIN)/build_hannk_h: util/build_hannk_h.cpp
	@-mkdir -p $(@D)
	$(CXX) -std=c++17 $< -o $@

HANNK_HEADERS = \
	interpreter/interpreter.h \
	interpreter/lower.h \
	interpreter/ops.h \
	util/error_util.h

$(BIN)/hannk.h: LICENSE.txt $(HANNK_HEADERS) $(GENERATOR_BIN)/build_hannk_h
	@mkdir -p $(@D)
	$(GENERATOR_BIN)/build_hannk_h LICENSE.txt $(MAKEFILE_DIR) $(HANNK_HEADERS) > $@

$(BIN)/%/libHannk.a: $(INTERPRETER_DEPS) $(UTIL_DEPS)
	@mkdir -p $(@D)
	@rm -f $@
	$(AR-$*) q $@ $^
	$(AR-$*) s $@

LIBHANNK_H = $(BIN)/hannk.h

$(BIN)/%/model_runner.o: util/model_runner.cpp $(LIBHANNK_H)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -I$(TFLITE_INCLUDES) -I$(dir $(LIBHANNK_H)) -c $< -o $@

MODEL_RUNNER_DEPS = \
	$(BIN)/%/model_runner.o

# ---------------------- tflite-parser

TFLITE_SCHEMA_CXXFLAGS = -I$(TFLITE_INCLUDES) -I$(FLATBUFFER_INCLUDES)

$(BIN)/%/tflite_parser.o: tflite/tflite_parser.cpp $(LIBHANNK_H)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(TFLITE_SCHEMA_CXXFLAGS) -I$(dir $(LIBHANNK_H)) -c $< -o $@

TFLITE_PARSER_DEPS = \
	$(BIN)/%/tflite_parser.o

# ---------------------- delegate

TFLITE_INCLUDES_FLAGS ?= -I$(HOME)/GitHub/tensorflow -I$(MAKEFILE_DIR)

$(BIN)/%/hannk_delegate.o: delegate/hannk_delegate.cpp $(LIBHANNK_H)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(TFLITE_INCLUDES_FLAGS) $(UTIL_CXXFLAGS) -I$(dir $(LIBHANNK_H)) -c $< -o $@

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
		$(BIN)/%/libHannk.a
	@mkdir -p $(@D)
	$(CXX-$*) -shared $^ $(DELEGATE_LD_FLAGS) -o $@

HANNK_INTERNAL_DELEGATE_DEPS = \
	$(BIN)/%/hannk_delegate.o

# ---------------------- toplevel executables


$(BIN)/%/$(BENCHMARK_OUT): benchmark.cpp $(TFLITE_PARSER_DEPS) util/file_util.h $(BIN)/%/libHannk.a $(LIBHANNK_H)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(BENCHMARK_HEXAGON_FLAGS) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^)  -I$(dir $(LIBHANNK_H)) -o $@ $(LDFLAGS-$*)


# To build for Android, use `HL_TARGET=arm-64-android make compare_vs_tflite`
$(BIN)/%/compare_vs_tflite: compare_vs_tflite.cpp \
		$(TFLITE_PARSER_DEPS) \
		$(MODEL_RUNNER_DEPS) \
		$(TFLITE_SHARED_LIBRARY) \
		$(HANNK_INTERNAL_DELEGATE_DEPS) \
		$(BIN)/$(HL_TARGET)/libHannkDelegate.so \
		$(BIN)/$(HL_TARGET)/libHannk.a
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -I$(TFLITE_INCLUDES) $(TFLITE_LDFLAGS) $(filter %.cpp %.o %.a %.so %.$(SHARED_EXT),$^) -o $@ $(LDFLAGS-$*)

.PHONY: compare_vs_tflite

compare_vs_tflite: $(BIN)/$(HL_TARGET)/compare_vs_tflite

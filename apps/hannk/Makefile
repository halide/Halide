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

# Removing exceptions just because we don't need 'em and it saves space.
#
# -fPIC is necessary for .so builds (at least on Linux); not necessary for the non-delegate
# builds but easier to enable it for everything.
CXXFLAGS += -Wno-unused-private-field -fno-exceptions -fPIC -fvisibility=hidden -fvisibility-inlines-hidden

MAKEFILE_DIR=$(dir $(realpath $(firstword $(MAKEFILE_LIST))))

.PHONY: all build clean test build_test_ops delegate
all: build

# .SECONDARY with no prerequisites causes all targets to be treated as secondary
# (i.e., no target is removed because it is considered intermediate).
.SECONDARY:

build: \
	$(BIN)/$(HL_TARGET)/benchmark \
	$(BIN)/$(HL_TARGET)/compare_vs_tflite \
	build_test_ops

test: test_ops

clean:
	rm -rf $(BIN)

# Choosing TFLite 2.4.0 because it's the most recent stable release with an Android AAR file available.
TFLITE_VERSION_MAJOR ?= 2
TFLITE_VERSION_MINOR ?= 4
TFLITE_VERSION_PATCH ?= 0

TFLITE_VERSION = $(TFLITE_VERSION_MAJOR).$(TFLITE_VERSION_MINOR).$(TFLITE_VERSION_PATCH)
TFLITE_TAG = v$(TFLITE_VERSION)

# Define `TFLITE_VERSION` here to allow for code that compiles against multiple versions of
# TFLite (both the C API and the Schema). This deliberately ignores the 'patch' version so
# 2.3.x is 23, 2.4.x is 24, etc. (Yes, this is inadequate if a minor version ever goes above 9.)
APP_CXXFLAGS = -I$(MAKEFILE_DIR) -DTFLITE_VERSION=$(TFLITE_VERSION_MAJOR)$(TFLITE_VERSION_MINOR)

# ---------------------- halide

$(GENERATOR_BIN)/common_halide.o: halide/common_halide.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(GENERATOR_BIN)/%.generator: halide/%_generator.cpp $(GENERATOR_BIN)/common_halide.o $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp %.o,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/add_uint8_uint8.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	$< -g Add -f hannk::add_uint8_uint8 -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/average_pool_uint8.a: $(GENERATOR_BIN)/average_pool.generator
	@mkdir -p $(@D)
	$< -g AveragePool -f hannk::average_pool_uint8 -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/convolution_uint8.a: $(GENERATOR_BIN)/convolution.generator
	@mkdir -p $(@D)
	$< -g Convolution -f hannk::convolution_uint8 -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/depthwise_convolution_uint8.a: $(GENERATOR_BIN)/depthwise_convolution.generator
	@mkdir -p $(@D)
	$< -g DepthwiseConvolution -f hannk::depthwise_convolution_uint8 -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/depthwise_convolution_uint8_broadcast.a: $(GENERATOR_BIN)/depthwise_convolution.generator
	@mkdir -p $(@D)
	$< -g DepthwiseConvolution broadcast_channels=true -f hannk::depthwise_convolution_uint8_broadcast -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/fully_connected_uint8.a: $(GENERATOR_BIN)/fully_connected.generator
	@mkdir -p $(@D)
	$< -g FullyConnected -f hannk::fully_connected_uint8 -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

$(BIN)/%/max_pool_uint8.a: $(GENERATOR_BIN)/max_pool.generator
	@mkdir -p $(@D)
	$< -g MaxPool -f hannk::max_pool_uint8 -o $(BIN)/$* target=$(HL_TARGET)-no_runtime-c_plus_plus_name_mangling

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

$(BIN)/%/error_util.o: util/error_util.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -c $< -o $@

UTIL_DEPS = \
	$(BIN)/%/error_util.o

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

$(BIN)/schema/tflite_schema.fbs:
	@echo Fetching tflite_schema.fbs...
	@mkdir -p $(@D)
	@wget --quiet -O $@ https://github.com/tensorflow/tensorflow/raw/$(TFLITE_TAG)/tensorflow/lite/schema/schema.fbs || rm -f $@

# This is a very minimal .h file that allows only for reading a flatbuffer...
# which is all tflite_parser needs.
$(BIN)/schema/tflite_schema_generated.h: $(BIN)/schema/tflite_schema.fbs
	@mkdir -p $(@D)
	$(FLATC) --cpp --no-includes -o $(@D) $<

TFLITE_SCHEMA_CXXFLAGS = -I$(BIN)/schema

$(BIN)/%/tflite_parser.o: tflite/tflite_parser.cpp $(BIN)/schema/tflite_schema_generated.h
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -I$(FLATBUFFERS_INCLUDE_PATH) $(TFLITE_SCHEMA_CXXFLAGS) -c $< -o $@

$(BIN)/%/tflite_parser.o: tflite/tflite_parser.cpp $(BIN)/schema/tflite_schema_generated.h
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -I$(FLATBUFFERS_INCLUDE_PATH) $(TFLITE_SCHEMA_CXXFLAGS) -c $< -o $@

TFLITE_PARSER_DEPS = \
	$(BIN)/%/tflite_parser.o

# ---------------------- delegate

TFLITE_INCLUDES_FLAGS ?= -I$(HOME)/GitHub/tensorflow -I$(MAKEFILE_DIR)

$(BIN)/%/halide_delegate.o: delegate/halide_delegate.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(TFLITE_INCLUDES_FLAGS) $(UTIL_CXXFLAGS) -c $< -o $@

$(BIN)/%/halide_delegate_adaptor.o: delegate/halide_delegate_adaptor.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(TFLITE_INCLUDES_FLAGS) $(UTIL_CXXFLAGS) -c $< -o $@

ifneq (,$(findstring host,$(HL_TARGET)))

DELEGATE_LD_FLAGS=$(LDFLAGS)

ifeq ($(UNAME), Darwin)
DELEGATE_LD_FLAGS += -Wl,-exported_symbols_list delegate/exported_symbols.osx
else
# Assume Desktop Linux
DELEGATE_LD_FLAGS += -Wl,--version-script=delegate/exported_symbols.ldscript
DELEGATE_LD_FLAGS += -Wl,-soname,libHalideDelegate.so
endif

else ifeq (arm-64-android,$(findstring arm-64-android,$(HL_TARGET)))

# Don't include $(LDFLAGS) here, it includes pthreads, which we don't want
DELEGATE_LD_FLAGS=-static-libstdc++
DELEGATE_LD_FLAGS += -Wl,--version-script=delegate/exported_symbols.ldscript
DELEGATE_LD_FLAGS += -Wl,-soname,libHalideDelegate.so

else

# Other values for HL_TARGET will be special-cased here in the future.
# Unhandled cases will fail to compile/link.
DELEGATE_LD_FLAGS ?= ERROR_TODO

endif

# Note: delegates are apparently always .so on OSX (never .dylib)
$(BIN)/%/libHalideDelegate.so: \
		$(BIN)/%/halide_delegate.o \
		$(BIN)/%/halide_delegate_adaptor.o \
		$(INTERPRETER_DEPS) \
		$(UTIL_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) -shared $^ $(DELEGATE_LD_FLAGS) -o $@

delegate: $(BIN)/$(HL_TARGET)/libHalideDelegate.so

# ---------------------- test

# unit tests for each op (not yet complete)
$(BIN)/%/add_test: test/add_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE) $(UTIL_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/average_pool_test: test/average_pool_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE) $(UTIL_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/concatenation_test: test/concatenation_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE) $(UTIL_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/conv2d_test: test/conv2d_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE) $(UTIL_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/depthwise_conv2d_test: test/depthwise_conv2d_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE) $(UTIL_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/fully_connected_test: test/fully_connected_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE) $(UTIL_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/max_pool_test: test/max_pool_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE) $(UTIL_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/pad_test: test/pad_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE) $(UTIL_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/quantize_test: test/quantize_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE) $(UTIL_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

$(BIN)/%/reshape_test: test/reshape_test.cpp $(BIN)/%/model.o $(BIN)/%/interval.o $(BIN)/%/ops.o $(OPS_HALIDE) $(UTIL_DEPS)
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

$(BIN)/%/benchmark: benchmark.cpp $(INTERPRETER_DEPS) $(TFLITE_PARSER_DEPS) $(UTIL_DEPS)
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) $(filter %.cpp %.o %.a,$^) -o $@ $(LDFLAGS-$*)

# compare_vs_tflite requires the tflite headers and library.
#
# For host, you need to clone and build locally (if there is a prebuilt
# library for TFLite for linux, etc # that's generally available, I haven't
# found it, except for the Python plugins, which won't work for our purposes).
# Note that we require the C API for TFLite (not the C++ API); building
# `//tensorflow/lite:libtensorflowlite.so` will only provide the C++ API. To
# get the C API version, use `bazel build -c opt //tensorflow/lite/c:tensorflowlite_c`
# instead.
#
# It's inexplicably painful to build TFLite in library form for Android (either in
# static or shared form), so we pull a prebuilt version (which is what they recommend
# anyway). For Android we pull the libtensorflowlite_jni.so build.

ifneq (,$(findstring host,$(HL_TARGET)))

# Normally, TENSORFLOW_BASE is the only symbol you need to define.
# (This seems like a plausible default.)
#
# Sample steps to build:
#
# 	$ git clone https://github.com/tensorflow/tensorflow
# 	$ cd tensorflow
#   $ git checkout $(TFLITE_TAG)
# 	$ bazelisk build -c opt //tensorflow/lite/c:tensorflowlite_c
#
# (Note that TFLite is very picky about the version of Bazel installed, thus this suggests
# using bazelisk rather than bazel; see https://github.com/bazelbuild/bazelisk)

TENSORFLOW_BASE ?= $(HOME)/GitHub/tensorflow
TFLITE_INCLUDES ?= $(TENSORFLOW_BASE)
TFLITE_SHARED_LIBRARY ?= $(TENSORFLOW_BASE)/bazel-bin/tensorflow/lite/c/libtensorflowlite_c.$(SHARED_EXT)
# Point at the TFLite path, but also the exe directory (for libHalideDelegate.so)
TFLITE_LDFLAGS=-Wl,-rpath,$(dir $(TFLITE_SHARED_LIBRARY)),-rpath,$(dir $@)

else ifeq (arm-64-android,$(findstring arm-64-android,$(HL_TARGET)))

# See https://www.tensorflow.org/lite/guide/android for details of what we're doing here
# to get an Android prebuilt tflite library.
$(BIN)/tflite-android/jni/arm64-v8a/libtensorflowlite_jni.so:
	@echo Fetching tensorflow-lite-android.zip...
	@mkdir -p $(@D)
	@wget --quiet -O $(BIN)/tflite-android.zip https://google.bintray.com/tensorflow/org/tensorflow/tensorflow-lite/$(TFLITE_VERSION)/tensorflow-lite-$(TFLITE_VERSION).aar || rm -f $@
	@echo Unzipping tensorflow-lite-android.zip...
	@unzip -q -o $(BIN)/tflite-android.zip -d $(BIN)/tflite-android

TFLITE_INCLUDES=$(BIN)/tflite-android/headers
TFLITE_SHARED_LIBRARY=$(BIN)/tflite-android/jni/arm64-v8a/libtensorflowlite_jni.so
# Set the rpath to . on Android since run_compare_on_device.sh will push the .so to the same dir as the app
TFLITE_LDFLAGS=-Wl,-rpath,.

else

# Other values for HL_TARGET will be special-cased here in the future.
# Unhandled cases will fail to compile/link.
TFLITE_INCLUDES ?= ERROR_TODO
TFLITE_SHARED_LIBRARY ?= ERROR_TODO

endif


# To build for Android, use `HL_TARGET=arm-64-android make compare_vs_tflite`
$(BIN)/%/compare_vs_tflite: compare_vs_tflite.cpp $(INTERPRETER_DEPS) $(TFLITE_PARSER_DEPS) $(UTIL_DEPS) $(TFLITE_SHARED_LIBRARY) $(BIN)/$(HL_TARGET)/libHalideDelegate.so
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(APP_CXXFLAGS) -I$(TFLITE_INCLUDES) $(TFLITE_LDFLAGS) $(filter %.cpp %.o %.a %.so %.$(SHARED_EXT),$^) -o $@ $(LDFLAGS-$*)

.PHONY: compare_vs_tflite

compare_vs_tflite: $(BIN)/$(HL_TARGET)/compare_vs_tflite


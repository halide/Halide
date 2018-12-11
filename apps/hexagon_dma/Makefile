# Ensure that make with no arguments makes the right thing
all:

include ../support/Makefile.inc

# This app requires a separate toolchain to be built from the Android NDK,
# using the make-standalone-toolchain.sh script:
#$ build/tools/make-standalone-toolchain.sh --arch=arm64 --platform=android-21 --install-dir=$ANDROID_ARM64_TOOLCHAIN
#$ build/tools/make-standalone-toolchain.sh --arch=arm --platform=android-21 --install-dir=$ANDROID_ARM_TOOLCHAIN

#to run the specialized tests optional arguments are
#$(BIN)/process_nv12_linear_ro_basic-host  width height option( where option is pipeline basic, fold, async, split, split_fold )
CXX-host ?= $(CXX)
CXX-arm-64-android ?= $(ANDROID_ARM64_TOOLCHAIN)/bin/aarch64-linux-android-c++
CXX-arm-32-android ?= $(ANDROID_ARM_TOOLCHAIN)/bin/arm-linux-androideabi-c++
CXX-arm-64-profile-android ?= $(CXX-arm-64-android)
CXX-arm-32-profile-android ?= $(CXX-arm-32-android)

CXXFLAGS-host ?=
CXXFLAGS-arm-64-android ?=
CXXFLAGS-arm-32-android ?=

LDFLAGS-host ?= -lpthread -ldl -lm
LDFLAGS-arm-64-android ?= -llog -fPIE -pie
LDFLAGS-arm-32-android ?= -llog -fPIE -pie
LDFLAGS-arm-64-profile-android ?= -llog -fPIE -pie
LDFLAGS-arm-32-profile-android ?= -llog -fPIE -pie
OPTION := none

BIN ?= bin

all: $(BIN)/process_nv12_linear_ro_basic-host \
     $(BIN)/process_nv12_linear_rw_basic-host \
     $(BIN)/process_p010_linear_ro_basic-host \
     $(BIN)/process_p010_linear_rw_basic-host \
     $(BIN)/process_raw_linear_ro_basic_interleaved-host \

$(BIN)/pipeline_yuv_linear_basic: pipeline_yuv_linear_basic.cpp $(GENERATOR_DEPS)
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)
$(BIN)/pipeline_raw_linear_ro_basic_interleaved: pipeline_raw_linear_ro_basic_interleaved.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

INPUT_YUV_8BIT = input_y.type=uint8 input_uv.type=uint8
INPUT_YUV_16BIT = input_y.type=uint16 input_uv.type=uint16
OUTPUT_YUV_8BIT = output_y.type=uint8 output_uv.type=uint8
OUTPUT_YUV_16BIT = output_y.type=uint16 output_uv.type=uint16

$(BIN)/%/pipeline_nv12_linear_ro_async.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_nv12_linear_ro_async target=$*-hexagon_dma schedule=async $(INPUT_YUV_8BIT) $(OUTPUT_YUV_8BIT) use_dma_for_output=false
$(BIN)/%/pipeline_nv12_linear_ro_basic.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_nv12_linear_ro_basic target=$*-hexagon_dma schedule=none $(INPUT_YUV_8BIT) $(OUTPUT_YUV_8BIT) use_dma_for_output=false
$(BIN)/%/pipeline_nv12_linear_ro_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_nv12_linear_ro_fold target=$*-hexagon_dma schedule=fold $(INPUT_YUV_8BIT) $(OUTPUT_YUV_8BIT) use_dma_for_output=false
$(BIN)/%/pipeline_nv12_linear_ro_split.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_nv12_linear_ro_split target=$*-hexagon_dma schedule=split $(INPUT_YUV_8BIT) $(OUTPUT_YUV_8BIT) use_dma_for_output=false
$(BIN)/%/pipeline_nv12_linear_ro_split_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_nv12_linear_ro_split_fold target=$*-hexagon_dma schedule=split_fold $(INPUT_YUV_8BIT) $(OUTPUT_YUV_8BIT) use_dma_for_output=false
$(BIN)/%/pipeline_nv12_linear_rw_async.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_nv12_linear_rw_async target=$*-hexagon_dma-debug schedule=async $(INPUT_YUV_8BIT) $(OUTPUT_YUV_8BIT) use_dma_for_output=true
$(BIN)/%/pipeline_nv12_linear_rw_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_nv12_linear_rw_fold target=$*-hexagon_dma-debug schedule=fold $(INPUT_YUV_8BIT) $(OUTPUT_YUV_8BIT) use_dma_for_output=true
$(BIN)/%/pipeline_nv12_linear_rw_basic.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_nv12_linear_rw_basic target=$*-hexagon_dma-debug schedule=none $(INPUT_YUV_8BIT) $(OUTPUT_YUV_8BIT) use_dma_for_output=true
$(BIN)/%/pipeline_nv12_linear_rw_split.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_nv12_linear_rw_split target=$*-hexagon_dma-debug schedule=split $(INPUT_YUV_8BIT) $(OUTPUT_YUV_8BIT) use_dma_for_output=true
$(BIN)/%/pipeline_nv12_linear_rw_split_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_nv12_linear_rw_split_fold target=$*-hexagon_dma-debug schedule=split_fold $(INPUT_YUV_8BIT) $(OUTPUT_YUV_8BIT) use_dma_for_output=true
$(BIN)/%/pipeline_p010_linear_ro_async.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_p010_linear_ro_async target=$*-hexagon_dma schedule=async $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=false
$(BIN)/%/pipeline_p010_linear_ro_basic.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_p010_linear_ro_basic target=$*-hexagon_dma schedule=none $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=false
$(BIN)/%/pipeline_p010_linear_ro_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_p010_linear_ro_fold target=$*-hexagon_dma schedule=fold $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=false
$(BIN)/%/pipeline_p010_linear_ro_split_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_p010_linear_ro_split_fold target=$*-hexagon_dma schedule=split_fold $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=false
$(BIN)/%/pipeline_p010_linear_ro_split.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_p010_linear_ro_split target=$*-hexagon_dma schedule=split $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=false
$(BIN)/%/pipeline_p010_linear_rw_async.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_p010_linear_rw_async target=$*-hexagon_dma schedule=async $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=true
$(BIN)/%/pipeline_p010_linear_rw_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_p010_linear_rw_fold target=$*-hexagon_dma schedule=fold $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=true
$(BIN)/%/pipeline_p010_linear_rw_basic.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_p010_linear_rw_basic target=$*-hexagon_dma schedule=none $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=true
$(BIN)/%/pipeline_p010_linear_rw_split.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_p010_linear_rw_split target=$*-hexagon_dma schedule=split $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=true
$(BIN)/%/pipeline_p010_linear_rw_split_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g pipeline_yuv_linear_basic -o $(BIN)/$* -e o,h,html -f pipeline_p010_linear_rw_split_fold target=$*-hexagon_dma schedule=split_fold $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=true
$(BIN)/%/pipeline_raw_linear_ro_async_interleaved.o: $(BIN)/pipeline_raw_linear_ro_basic_interleaved
	@mkdir -p $(@D)
	$^ -g pipeline_raw_linear_ro_basic_interleaved -o $(BIN)/$* -e o,h,html -f pipeline_raw_linear_ro_async_interleaved target=$*-hexagon_dma schedule=async
$(BIN)/%/pipeline_raw_linear_ro_basic_interleaved.o: $(BIN)/pipeline_raw_linear_ro_basic_interleaved
	@mkdir -p $(@D)
	$^ -g pipeline_raw_linear_ro_basic_interleaved -o $(BIN)/$* -e o,h,html -f pipeline_raw_linear_ro_basic_interleaved target=$*-hexagon_dma schedule=none
$(BIN)/%/pipeline_raw_linear_ro_fold_interleaved.o: $(BIN)/pipeline_raw_linear_ro_basic_interleaved
	@mkdir -p $(@D)
	$^ -g pipeline_raw_linear_ro_basic_interleaved -o $(BIN)/$* -e o,h,html -f pipeline_raw_linear_ro_fold_interleaved target=$*-hexagon_dma schedule=fold
$(BIN)/%/pipeline_raw_linear_ro_split_interleaved.o: $(BIN)/pipeline_raw_linear_ro_basic_interleaved
	@mkdir -p $(@D)
	$^ -g pipeline_raw_linear_ro_basic_interleaved -o $(BIN)/$* -e o,h,html -f pipeline_raw_linear_ro_split_interleaved target=$*-hexagon_dma schedule=split
$(BIN)/%/pipeline_raw_linear_ro_split_fold_interleaved.o: $(BIN)/pipeline_raw_linear_ro_basic_interleaved
	@mkdir -p $(@D)
	$^ -g pipeline_raw_linear_ro_basic_interleaved -o $(BIN)/$* -e o,h,html -f pipeline_raw_linear_ro_split_fold_interleaved target=$*-hexagon_dma schedule=split_fold

$(BIN)/process_nv12_linear_ro_basic-%: process_nv12_linear_ro_basic.cpp $(BIN)/%/pipeline_nv12_linear_ro_basic.o $(BIN)/%/pipeline_nv12_linear_ro_fold.o $(BIN)/%/pipeline_nv12_linear_ro_async.o $(BIN)/%/pipeline_nv12_linear_ro_split.o $(BIN)/%/pipeline_nv12_linear_ro_split_fold.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_nv12_linear_ro_basic.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_nv12_linear_ro_basic.o $(BIN)/$*/pipeline_nv12_linear_ro_fold.o $(BIN)/$*/pipeline_nv12_linear_ro_async.o $(BIN)/$*/pipeline_nv12_linear_ro_split.o $(BIN)/$*/pipeline_nv12_linear_ro_split_fold.o -o $(BIN)/process_nv12_linear_ro_basic-$* $(LDFLAGS-$*)
$(BIN)/process_nv12_linear_rw_basic-%: process_nv12_linear_rw_basic.cpp $(BIN)/%/pipeline_nv12_linear_rw_basic.o $(BIN)/%/pipeline_nv12_linear_rw_async.o $(BIN)/%/pipeline_nv12_linear_rw_fold.o $(BIN)/%/pipeline_nv12_linear_rw_split.o $(BIN)/%/pipeline_nv12_linear_rw_split_fold.o  mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_nv12_linear_rw_basic.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_nv12_linear_rw_basic.o $(BIN)/$*/pipeline_nv12_linear_rw_fold.o $(BIN)/$*/pipeline_nv12_linear_rw_async.o $(BIN)/$*/pipeline_nv12_linear_rw_split.o $(BIN)/$*/pipeline_nv12_linear_rw_split_fold.o -o $(BIN)/process_nv12_linear_rw_basic-$* $(LDFLAGS-$*)
$(BIN)/process_p010_linear_ro_basic-%: process_p010_linear_ro_basic.cpp $(BIN)/%/pipeline_p010_linear_ro_basic.o $(BIN)/%/pipeline_p010_linear_ro_basic.o $(BIN)/%/pipeline_p010_linear_ro_fold.o $(BIN)/%/pipeline_p010_linear_ro_async.o $(BIN)/%/pipeline_p010_linear_ro_split.o $(BIN)/%/pipeline_p010_linear_ro_split_fold.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_p010_linear_ro_basic.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_p010_linear_ro_basic.o $(BIN)/$*/pipeline_p010_linear_ro_fold.o $(BIN)/$*/pipeline_p010_linear_ro_async.o $(BIN)/$*/pipeline_p010_linear_ro_split.o $(BIN)/$*/pipeline_p010_linear_ro_split_fold.o -o $(BIN)/process_p010_linear_ro_basic-$* $(LDFLAGS-$*)
$(BIN)/process_p010_linear_rw_basic-%: process_p010_linear_rw_basic.cpp $(BIN)/%/pipeline_p010_linear_rw_basic.o $(BIN)/%/pipeline_p010_linear_rw_fold.o $(BIN)/%/pipeline_p010_linear_rw_async.o $(BIN)/%/pipeline_p010_linear_rw_split.o $(BIN)/%/pipeline_p010_linear_rw_split_fold.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_p010_linear_rw_basic.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_p010_linear_rw_basic.o $(BIN)/$*/pipeline_p010_linear_rw_fold.o $(BIN)/$*/pipeline_p010_linear_rw_async.o $(BIN)/$*/pipeline_p010_linear_rw_split.o $(BIN)/$*/pipeline_p010_linear_rw_split_fold.o -o $(BIN)/process_p010_linear_rw_basic-$* $(LDFLAGS-$*)
$(BIN)/process_raw_linear_ro_basic_interleaved-%: process_raw_linear_ro_basic_interleaved.cpp $(BIN)/%/pipeline_raw_linear_ro_basic_interleaved.o mock_dma_implementation.cpp $(BIN)/%/pipeline_raw_linear_ro_fold_interleaved.o $(BIN)/%/pipeline_raw_linear_ro_split_interleaved.o $(BIN)/%/pipeline_raw_linear_ro_split_fold_interleaved.o $(BIN)/%/pipeline_raw_linear_ro_async_interleaved.o
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_raw_linear_ro_basic_interleaved.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_raw_linear_ro_basic_interleaved.o $(BIN)/$*/pipeline_raw_linear_ro_fold_interleaved.o $(BIN)/$*/pipeline_raw_linear_ro_async_interleaved.o $(BIN)/$*/pipeline_raw_linear_ro_split_interleaved.o $(BIN)/$*/pipeline_raw_linear_ro_split_fold_interleaved.o -o $(BIN)/process_raw_linear_ro_basic_interleaved-$* $(LDFLAGS-$*)

clean:
	rm -rf $(BIN)

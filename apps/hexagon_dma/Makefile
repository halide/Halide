# Ensure that make with no arguments makes the right thing
all:

include ../support/Makefile.inc

# This app requires a separate toolchain to be built from the Android NDK,
# using the make-standalone-toolchain.sh script:
#$ build/tools/make-standalone-toolchain.sh --arch=arm64 --platform=android-21 --install-dir=$ANDROID_ARM64_TOOLCHAIN
#$ build/tools/make-standalone-toolchain.sh --arch=arm --platform=android-21 --install-dir=$ANDROID_ARM_TOOLCHAIN

#to run the specialized tests optional arguments are
#$(BIN)/process_nv12_linear_basic-host  width height schedule {where pipeline schedule is: basic, fold, async, split, split_fold} dma_rw {ro, rw} 
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

all: $(BIN)/process_nv12_linear_basic-host \
	 $(BIN)/process_p010_linear_basic-host \
	 $(BIN)/process_raw_linear_interleaved_basic-host \

$(BIN)/pipeline_yuv_linear_basic: pipeline_yuv_linear_basic.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)
$(BIN)/pipeline_raw_linear_interleaved_basic: pipeline_raw_linear_interleaved_basic.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

INPUT_YUV_8BIT = input_y.type=uint8 input_uv.type=uint8
INPUT_YUV_16BIT = input_y.type=uint16 input_uv.type=uint16
OUTPUT_YUV_8BIT = output_y.type=uint8 output_uv.type=uint8
OUTPUT_YUV_16BIT = output_y.type=uint16 output_uv.type=uint16

$(BIN)/%/pipeline_nv12_linear_ro_async.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma-debug schedule=async      $(INPUT_YUV_8BIT)  $(OUTPUT_YUV_8BIT)  use_dma_for_output=false
$(BIN)/%/pipeline_nv12_linear_ro_basic.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma-debug schedule=none       $(INPUT_YUV_8BIT)  $(OUTPUT_YUV_8BIT)  use_dma_for_output=false
$(BIN)/%/pipeline_nv12_linear_ro_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma-debug schedule=fold       $(INPUT_YUV_8BIT)  $(OUTPUT_YUV_8BIT)  use_dma_for_output=false
$(BIN)/%/pipeline_nv12_linear_ro_split.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma-debug schedule=split      $(INPUT_YUV_8BIT)  $(OUTPUT_YUV_8BIT)  use_dma_for_output=false
$(BIN)/%/pipeline_nv12_linear_ro_split_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma-debug schedule=split_fold $(INPUT_YUV_8BIT)  $(OUTPUT_YUV_8BIT)  use_dma_for_output=false
$(BIN)/%/pipeline_nv12_linear_rw_async.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma-debug schedule=async      $(INPUT_YUV_8BIT)  $(OUTPUT_YUV_8BIT)  use_dma_for_output=true
$(BIN)/%/pipeline_nv12_linear_rw_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma-debug schedule=fold       $(INPUT_YUV_8BIT)  $(OUTPUT_YUV_8BIT)  use_dma_for_output=true
$(BIN)/%/pipeline_nv12_linear_rw_basic.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma-debug schedule=none       $(INPUT_YUV_8BIT)  $(OUTPUT_YUV_8BIT)  use_dma_for_output=true
$(BIN)/%/pipeline_nv12_linear_rw_split.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma-debug schedule=split      $(INPUT_YUV_8BIT)  $(OUTPUT_YUV_8BIT)  use_dma_for_output=true
$(BIN)/%/pipeline_nv12_linear_rw_split_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma-debug schedule=split_fold $(INPUT_YUV_8BIT)  $(OUTPUT_YUV_8BIT)  use_dma_for_output=true
$(BIN)/%/pipeline_p010_linear_ro_async.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=async      $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=false
$(BIN)/%/pipeline_p010_linear_ro_basic.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=none       $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=false
$(BIN)/%/pipeline_p010_linear_ro_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=fold       $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=false
$(BIN)/%/pipeline_p010_linear_ro_split_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=split_fold $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=false
$(BIN)/%/pipeline_p010_linear_ro_split.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=split      $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=false
$(BIN)/%/pipeline_p010_linear_rw_async.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=async      $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=true
$(BIN)/%/pipeline_p010_linear_rw_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=fold       $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=true
$(BIN)/%/pipeline_p010_linear_rw_basic.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=none       $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=true
$(BIN)/%/pipeline_p010_linear_rw_split.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=split      $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=true
$(BIN)/%/pipeline_p010_linear_rw_split_fold.o: $(BIN)/pipeline_yuv_linear_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=split_fold $(INPUT_YUV_16BIT) $(OUTPUT_YUV_16BIT) use_dma_for_output=true
$(BIN)/%/pipeline_raw_linear_interleaved_ro_async.o: $(BIN)/pipeline_raw_linear_interleaved_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=async      use_dma_for_output=false
$(BIN)/%/pipeline_raw_linear_interleaved_ro_basic.o: $(BIN)/pipeline_raw_linear_interleaved_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=none       use_dma_for_output=false
$(BIN)/%/pipeline_raw_linear_interleaved_ro_fold.o: $(BIN)/pipeline_raw_linear_interleaved_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=fold       use_dma_for_output=false
$(BIN)/%/pipeline_raw_linear_interleaved_ro_split.o: $(BIN)/pipeline_raw_linear_interleaved_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=split      use_dma_for_output=false
$(BIN)/%/pipeline_raw_linear_interleaved_ro_split_fold.o: $(BIN)/pipeline_raw_linear_interleaved_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=split_fold use_dma_for_output=false
$(BIN)/%/pipeline_raw_linear_interleaved_rw_async.o: $(BIN)/pipeline_raw_linear_interleaved_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=async      use_dma_for_output=true
$(BIN)/%/pipeline_raw_linear_interleaved_rw_basic.o: $(BIN)/pipeline_raw_linear_interleaved_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=none       use_dma_for_output=true
$(BIN)/%/pipeline_raw_linear_interleaved_rw_fold.o: $(BIN)/pipeline_raw_linear_interleaved_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=fold       use_dma_for_output=true
$(BIN)/%/pipeline_raw_linear_interleaved_rw_split.o: $(BIN)/pipeline_raw_linear_interleaved_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=split      use_dma_for_output=true
$(BIN)/%/pipeline_raw_linear_interleaved_rw_split_fold.o: $(BIN)/pipeline_raw_linear_interleaved_basic
	@mkdir -p $(@D)
	$^ -g $(<F) -o $(BIN)/$* -e o,h,html -f $(subst .o,,$(@F)) target=$*-hexagon_dma schedule=split_fold use_dma_for_output=true

NV12_PIPE=pipeline_nv12_linear
NV12_RW=$(BIN)/%/$(NV12_PIPE)_rw_basic.o $(BIN)/%/$(NV12_PIPE)_rw_fold.o $(BIN)/%/$(NV12_PIPE)_rw_async.o $(BIN)/%/$(NV12_PIPE)_rw_split.o $(BIN)/%/$(NV12_PIPE)_rw_split_fold.o
NV12_RO=$(subst _rw_,_ro_,$(NV12_RW))
P010_RW=$(subst nv12,p010,$(NV12_RW))
P010_RO=$(subst nv12,p010,$(NV12_RO))
RAW_RW=$(subst nv12_linear,raw_linear_interleaved,$(NV12_RW))
RAW_RO=$(subst nv12_linear,raw_linear_interleaved,$(NV12_RO))

$(BIN)/process_nv12_linear_basic-%: process_nv12_linear_basic.cpp $(NV12_RO) $(NV12_RW) mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS-$*)
$(BIN)/process_p010_linear_basic-%: process_p010_linear_basic.cpp $(P010_RO) $(P010_RW) mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS-$*)
$(BIN)/process_raw_linear_interleaved_basic-%: process_raw_linear_interleaved_basic.cpp $(RAW_RO) $(RAW_RW) mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS-$*)

clean:
	rm -rf $(BIN)

# Ensure that make with no arguments makes the right thing
all:

include ../support/Makefile.inc

# This app requires a separate toolchain to be built from the Android NDK,
# using the make-standalone-toolchain.sh script:
#$ build/tools/make-standalone-toolchain.sh --arch=arm64 --platform=android-21 --install-dir=$ANDROID_ARM64_TOOLCHAIN
#$ build/tools/make-standalone-toolchain.sh --arch=arm --platform=android-21 --install-dir=$ANDROID_ARM_TOOLCHAIN
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

BIN ?= bin

all: $(BIN)/process-host $(BIN)/process_nv12-host $(BIN)/process_p010-host 

$(BIN)/pipeline: pipeline.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)
$(BIN)/pipeline_nv12: pipeline_nv12.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)
$(BIN)/pipeline_p010: pipeline_p010.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/%/pipeline.o: $(BIN)/pipeline
	@mkdir -p $(@D)
	$^ -g dma_pipeline -o $(BIN)/$* -e o,h,html -f pipeline target=$*-hexagon_dma
$(BIN)/%/pipeline_nv12.o: $(BIN)/pipeline_nv12
	@mkdir -p $(@D)
	$^ -g dma_pipeline_nv12 -o $(BIN)/$* -e o,h,html -f pipeline_nv12 target=$*-hexagon_dma
$(BIN)/%/pipeline_p010.o: $(BIN)/pipeline_p010
	@mkdir -p $(@D)
	$^ -g dma_pipeline_p010 -o $(BIN)/$* -e o,h,html -f pipeline_p010 target=$*-hexagon_dma

$(BIN)/process-%: process.cpp $(BIN)/%/pipeline.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline.o -o $(BIN)/process-$* $(LDFLAGS-$*)
$(BIN)/process_nv12-%: process_nv12.cpp $(BIN)/%/pipeline_nv12.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_nv12.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_nv12.o -o $(BIN)/process_nv12-$* $(LDFLAGS-$*)
$(BIN)/process_p010-%: process_p010.cpp $(BIN)/%/pipeline_p010.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_p010.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_p010.o -o $(BIN)/process_p010-$* $(LDFLAGS-$*)

run-host: $(BIN)/process-host
	$(BIN)/process-host 2048 1024

clean:
	rm -rf $(BIN)

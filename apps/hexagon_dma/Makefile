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

all: $(BIN)/process-host $(BIN)/process_nv12-host $(BIN)/process_p010-host $(BIN)/process_err-host $(BIN)/process_rd_wr-host $(BIN)/process_rawinterleaved-host $(BIN)/process_rawplanar-host $(BIN)/process_blur-host

$(BIN)/pipeline: pipeline.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)
$(BIN)/pipeline_nv12: pipeline_nv12.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)
$(BIN)/pipeline_p010: pipeline_p010.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)
$(BIN)/pipeline_rd_wr: pipeline_rd_wr.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)
$(BIN)/pipeline_err: pipeline_err.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)
$(BIN)/pipeline_rawinterleaved: pipeline_rawinterleaved.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)
$(BIN)/pipeline_rawplanar: pipeline_rawplanar.cpp $(GENERATOR_DEPS)
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)
$(BIN)/pipeline_blur: pipeline_blur.cpp $(GENERATOR_DEPS)
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
$(BIN)/%/pipeline_rd_wr.o: $(BIN)/pipeline_rd_wr
	@mkdir -p $(@D)
	$^ -g dma_pipeline_rd_wr -o $(BIN)/$* -e o,h,html -f pipeline_rd_wr target=$*-hexagon_dma
$(BIN)/%/pipeline_err.o: $(BIN)/pipeline_err
	@mkdir -p $(@D)
	$^ -g dma_pipeline_err -o $(BIN)/$* -e o,h,html -f pipeline_err target=$*-hexagon_dma
$(BIN)/%/pipeline_rawinterleaved.o: $(BIN)/pipeline_rawinterleaved
	@mkdir -p $(@D)
	$^ -g dma_pipeline -o $(BIN)/$* -e o,h,html -f pipeline_rawinterleaved target=$*-hexagon_dma
$(BIN)/%/pipeline_rawplanar.o: $(BIN)/pipeline_rawplanar
	@mkdir -p $(@D)
	$^ -g dma_pipeline -o $(BIN)/$* -e o,h,html -f pipeline_rawplanar target=$*-hexagon_dma
$(BIN)/%/pipeline_blur.o: $(BIN)/pipeline_blur
	@mkdir -p $(@D)
	$^ -g dma_pipeline_blur -o $(BIN)/$* -e o,h,html -f pipeline_blur target=$*-hexagon_dma

$(BIN)/process-%: process.cpp $(BIN)/%/pipeline.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline.o -o $(BIN)/process-$* $(LDFLAGS-$*)
$(BIN)/process_nv12-%: process_nv12.cpp $(BIN)/%/pipeline_nv12.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_nv12.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_nv12.o -o $(BIN)/process_nv12-$* $(LDFLAGS-$*)
$(BIN)/process_p010-%: process_p010.cpp $(BIN)/%/pipeline_p010.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_p010.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_p010.o -o $(BIN)/process_p010-$* $(LDFLAGS-$*)
$(BIN)/process_rd_wr-%: process_rd_wr.cpp $(BIN)/%/pipeline_rd_wr.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_rd_wr.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_rd_wr.o -o $(BIN)/process_rd_wr-$* $(LDFLAGS-$*)
$(BIN)/process_err-%: process_err.cpp $(BIN)/%/pipeline_err.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_err.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_err.o -o $(BIN)/process_err-$* $(LDFLAGS-$*)
$(BIN)/process_rawinterleaved-%: process_rawinterleaved.cpp $(BIN)/%/pipeline_rawinterleaved.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_rawinterleaved.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_rawinterleaved.o -o $(BIN)/process_rawinterleaved-$* $(LDFLAGS-$*)
$(BIN)/process_rawplanar-%: process_rawplanar.cpp $(BIN)/%/pipeline_rawplanar.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_rawplanar.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_rawplanar.o -o $(BIN)/process_rawplanar-$* $(LDFLAGS-$*)
$(BIN)/process_blur-%: process_blur.cpp $(BIN)/%/pipeline_blur.o mock_dma_implementation.cpp
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process_blur.cpp mock_dma_implementation.cpp $(BIN)/$*/pipeline_blur.o -o $(BIN)/process_blur-$* $(LDFLAGS-$*)

run-host: $(BIN)/process-host
	$(BIN)/process-host 2048 1024

clean:
	rm -rf $(BIN)

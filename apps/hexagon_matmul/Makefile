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

all: $(BIN)/process-host $(BIN)/process-arm-64-android $(BIN)/process-arm-32-android $(BIN)/process-arm-64-profile-android $(BIN)/process-arm-32-profile-android

$(BIN)/pipeline: pipeline.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/%/pipeline_cpu.o: $(BIN)/pipeline
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f pipeline_cpu target=$*

$(BIN)/%/pipeline_hvx64.o: $(BIN)/pipeline
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f pipeline_hvx64 target=$*-hvx_64

$(BIN)/%/pipeline_hvx128.o: $(BIN)/pipeline
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f pipeline_hvx128 target=$*-hvx_128

$(BIN)/process-%: process.cpp $(BIN)/%/pipeline_cpu.o $(BIN)/%/pipeline_hvx64.o $(BIN)/%/pipeline_hvx128.o
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process.cpp $(BIN)/$*/pipeline_cpu.o $(BIN)/$*/pipeline_hvx64.o $(BIN)/$*/pipeline_hvx128.o -o $(BIN)/process-$* $(LDFLAGS-$*)

# This requires root: adb root && adb wait-for-device && adb remount
run-%-android: $(BIN)/process-%-android
	adb push $(BIN)/process-$*-android /data/
	adb shell chmod +x /data/process-$*-android
	adb shell /data/process-$*-android cpu 10 1024 1024 1024
	adb shell /data/process-$*-android hvx64 10 1024 1024 1024
	adb shell /data/process-$*-android hvx128 10 1024 1024 1024

run-host: $(BIN)/process-host
	$(BIN)/process-host cpu 10 256 256 256
	$(BIN)/process-host hvx64 1 256 256 256
	$(BIN)/process-host hvx128 1 256 256 256

clean:
	rm -rf $(BIN)

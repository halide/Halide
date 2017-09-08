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
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/%/pipeline_cpu.o: $(BIN)/pipeline
	@mkdir -p $(@D)
	$^ -o $(BIN)/$* -e o,h -f pipeline_cpu target=$*

$(BIN)/%/pipeline_hvx64.o: $(BIN)/pipeline
	@mkdir -p $(@D)
	$^ -o $(BIN)/$* -e o,h -f pipeline_hvx64 target=$*-hvx_64

$(BIN)/%/pipeline_hvx128.o: $(BIN)/pipeline
	@mkdir -p $(@D)
	$^ -o $(BIN)/$* -e o,h -f pipeline_hvx128 target=$*-hvx_128

$(BIN)/process-%: process.cpp $(BIN)/%/pipeline_cpu.o $(BIN)/%/pipeline_hvx64.o $(BIN)/%/pipeline_hvx128.o
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process.cpp $(BIN)/$*/pipeline_cpu.o $(BIN)/$*/pipeline_hvx64.o $(BIN)/$*/pipeline_hvx128.o -o $(BIN)/process-$* $(LDFLAGS-$*)

DEVICE_PATH ?= /data/local/tmp/HelloHexagon
DEVICE_ENV = "LD_LIBRARY_PATH=$(DEVICE_PATH):/vendor/lib64 ADSP_LIBRARY_PATH=\"$(DEVICE_PATH);/dsp\""
HEXAGON_RUNTIME_PATH = $(HALIDE_SRC_PATH)/src/runtime/hexagon_remote
run-%-android: $(BIN)/process-%-android
	adb shell mkdir -p $(DEVICE_PATH)
	adb push $(BIN)/process-$*-android $(DEVICE_PATH)
	adb push $(HEXAGON_RUNTIME_PATH)/bin/$*-android/libhalide_hexagon_host.so $(DEVICE_PATH)
	adb push $(HEXAGON_RUNTIME_PATH)/bin/v60/signed_by_debug/libhalide_hexagon_remote_skel.so $(DEVICE_PATH)
	adb shell cp /system/lib/rfsa/adsp/testsig* $(DEVICE_PATH) > /dev/null || true
	adb shell chmod +x $(DEVICE_PATH)/process-$*-android
	adb shell $(DEVICE_ENV) $(DEVICE_PATH)/process-$*-android cpu 10
	adb shell $(DEVICE_ENV) $(DEVICE_PATH)/process-$*-android hvx64 10
	adb shell $(DEVICE_ENV) $(DEVICE_PATH)/process-$*-android hvx128 10

run-host: $(BIN)/process-host
	$(BIN)/process-host cpu 10
	$(BIN)/process-host hvx64 1
	$(BIN)/process-host hvx128 1

clean:
	rm -rf $(BIN)

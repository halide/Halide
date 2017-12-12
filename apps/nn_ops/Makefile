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

all: $(BIN)/MatrixMultiply-host $(BIN)/MatrixMultiply-arm-64-android $(BIN)/MatrixMultiply-arm-32-android $(BIN)/MatrixMultiply-arm-64-profile-android $(BIN)/MatrixMultiply-arm-32-profile-android

$(BIN)/MatrixMultiply.generator: MatrixMultiply_generator.cpp common.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/%/MatrixMultiply_cpu.o: $(BIN)/MatrixMultiply.generator
	@mkdir -p $(@D)
	$^ -g MatrixMultiply -o $(BIN)/$* -e o,h -f MatrixMultiply_cpu target=$*

$(BIN)/%/MatrixMultiply_hvx64.o: $(BIN)/MatrixMultiply.generator
	@mkdir -p $(@D)
	$^ -g MatrixMultiply -o $(BIN)/$* -e o,h -f MatrixMultiply_hvx64 target=$*-hvx_64

$(BIN)/%/MatrixMultiply_hvx128.o: $(BIN)/MatrixMultiply.generator
	@mkdir -p $(@D)
	$^ -g MatrixMultiply -o $(BIN)/$* -e o,h -f MatrixMultiply_hvx128 target=$*-hvx_128

$(BIN)/MatrixMultiply-%: MatrixMultiply.cpp $(BIN)/%/MatrixMultiply_cpu.o $(BIN)/%/MatrixMultiply_hvx64.o $(BIN)/%/MatrixMultiply_hvx128.o
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 MatrixMultiply.cpp $(BIN)/$*/MatrixMultiply_cpu.o $(BIN)/$*/MatrixMultiply_hvx64.o $(BIN)/$*/MatrixMultiply_hvx128.o -o $(BIN)/MatrixMultiply-$* $(LDFLAGS-$*)

DEVICE_PATH ?= /data/local/tmp/nn_ops
DEVICE_ENV = "LD_LIBRARY_PATH=$(DEVICE_PATH):/vendor/lib64 ADSP_LIBRARY_PATH=\"$(DEVICE_PATH);/dsp\""
HEXAGON_RUNTIME_PATH = $(HALIDE_SRC_PATH)/src/runtime/hexagon_remote
run-%-android: $(BIN)/MatrixMultiply-%-android
	adb shell mkdir -p $(DEVICE_PATH)
	adb push $(BIN)/MatrixMultiply-$*-android $(DEVICE_PATH)
	adb push $(HEXAGON_RUNTIME_PATH)/bin/$*-android/libhalide_hexagon_host.so $(DEVICE_PATH)
	adb push $(HEXAGON_RUNTIME_PATH)/bin/v60/signed_by_debug/libhalide_hexagon_remote_skel.so $(DEVICE_PATH)
	adb shell cp /system/lib/rfsa/adsp/testsig* $(DEVICE_PATH) > /dev/null || true
	adb shell chmod +x $(DEVICE_PATH)/MatrixMultiply-$*-android
	adb push MatrixMultiply.sh $(DEVICE_PATH)
	adb shell $(DEVICE_ENV) $(DEVICE_PATH)/MatrixMultiply.sh MatrixMultiply-arm-64-android

run-host: $(BIN)/MatrixMultiply-host
	./MatrixMultiply.sh $(BIN)/MatrixMultiply-host

clean:
	rm -rf $(BIN)

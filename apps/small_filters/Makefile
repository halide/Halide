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

$(BIN)/conv3x3a16: conv3x3a16.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	echo "${GENERATOR_DEPS}"
	$(CXX) $(CXXFLAGS) -O3 -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/%/conv3x3a16_cpu.o: $(BIN)/conv3x3a16
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f conv3x3a16_cpu target=$*

$(BIN)/%/conv3x3a16_hvx64.o: $(BIN)/conv3x3a16
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f conv3x3a16_hvx64 target=$*-hvx_64

$(BIN)/%/conv3x3a16_hvx128.o: $(BIN)/conv3x3a16
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f conv3x3a16_hvx128 target=$*-hvx_128

$(BIN)/process-%: process.cpp $(BIN)/%/conv3x3a16_hvx128.o $(BIN)/%/conv3x3a16_hvx64.o $(BIN)/%/conv3x3a16_cpu.o
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process.cpp  $(BIN)/$*/conv3x3a16_hvx128.o  $(BIN)/$*/conv3x3a16_hvx64.o $(BIN)/$*/conv3x3a16_cpu.o -o $(BIN)/process-$* $(LDFLAGS-$*)

run-%-android: $(BIN)/process-%-android
	adb push $(BIN)/process-$*-android /data/
	adb shell chmod +x /data/process-$*-android
	adb shell /data/process-$*-android -n 10

#run-host: $(BIN)/process-host
#	$(BIN)/process-host cpu 10
#	$(BIN)/process-host hvx64 1
#	$(BIN)/process-host hvx128 1


clean:
	rm -rf $(BIN)

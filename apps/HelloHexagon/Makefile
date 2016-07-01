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

all: process-host process-arm-64-android process-arm-32-android process-arm-64-profile-android process-arm-32-profile-android

pipeline: pipeline.cpp
	$(CXX) $(CXXFLAGS) pipeline.cpp -g $(LIB_HALIDE) -o pipeline \
	$(LDFLAGS) $(LLVM_SHARED_LIBS)

pipeline_cpu-%.o: pipeline
	HL_TARGET=$* ./pipeline pipeline_cpu-$* pipeline_cpu

pipeline_hvx64-%.o: pipeline
	HL_TARGET=$*-hvx_64 ./pipeline pipeline_hvx64-$* pipeline_hvx64

pipeline_hvx128-%.o: pipeline
	HL_TARGET=$*-hvx_128 ./pipeline pipeline_hvx128-$* pipeline_hvx128

process-%: process.cpp pipeline_cpu-%.o pipeline_hvx64-%.o pipeline_hvx128-%.o
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -Wall -O3 process.cpp pipeline_cpu-$*.o pipeline_hvx64-$*.o pipeline_hvx128-$*.o -o process-$* $(LDFLAGS-$*)

# This requires root: adb root && adb wait-for-device && adb remount
run-%-android: process-%-android
	adb push process-$*-android /data/
	adb shell chmod +x /data/process-$*-android
	adb shell /data/process-$*-android cpu 10
	adb shell /data/process-$*-android hvx64 10
	adb shell /data/process-$*-android hvx128 10

run-host: process-host
	./process-host cpu 10
	./process-host hvx64 1
	./process-host hvx128 1

clean:
	rm -f process-* pipeline-*.o pipeline pipeline.h

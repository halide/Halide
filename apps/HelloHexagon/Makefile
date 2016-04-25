include ../support/Makefile.inc

# This app requires a separate toolchain to be built from the Android NDK, 
# using the make-standalone-toolchain.sh script:
#$ build/tools/make-standalone-toolchain.sh --arch=arm64 --platform=android-21 --install-dir=$ANDROID_ARM64_TOOLCHAIN
#$ build/tools/make-standalone-toolchain.sh --arch=arm --platform=android-21 --install-dir=$ANDROID_ARM_TOOLCHAIN
CXX-host ?= $(CXX)
CXX-arm-64-android ?= $(ANDROID_ARM64_TOOLCHAIN)/bin/aarch64-linux-android-c++
CXX-arm-32-android ?= $(ANDROID_ARM_TOOLCHAIN)/bin/arm-linux-android-c++

CXXFLAGS-host ?=
CXXFLAGS-arm-64-android ?=
CXXFLAGS-arm-32-android ?=

LDFLAGS-host ?= -lpthread -ldl -lm
LDFLAGS-arm-64-android ?= -llog -fPIE -pie
LDFLAGS-arm-32-android ?= -llog -fPIE -pie

all: process-host process-arm-64-android process-arm-32-android

pipeline: pipeline.cpp
	$(CXX) $(CXXFLAGS) pipeline.cpp -g $(LIB_HALIDE) -o pipeline \
	$(LDFLAGS) $(LLVM_SHARED_LIBS)

pipeline-%.o: pipeline
	HL_TARGET=$*-debug-hvx_64 ./pipeline $*

process-%: process.cpp pipeline-%.o
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -Wall -O3 process.cpp pipeline-$*.o -o process-$* $(LDFLAGS-$*)

run-%: process-%
	adb push process-$* /data/
	adb shell chmod +x /data/process-$*
	adb shell /data/process-$*

clean:
	rm -f process-* pipeline-*.o pipeline pipeline.h

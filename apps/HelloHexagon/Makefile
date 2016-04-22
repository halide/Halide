include ../support/Makefile.inc

CXX-host ?= $(CXX)
CXX-arm-64-android ?= $(ANDROID_NDK_HOME)/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-c++
CXX-arm-32-android ?= $(ANDROID_NDK_HOME)/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-c++

CXXFLAGS-host ?=
CXXFLAGS-arm-64-android ?= --sysroot $(ANDROID_NDK_HOME)/platforms/android-21/arch-arm64
CXXFLAGS-arm-32-android ?= --sysroot $(ANDROID_NDK_HOME)/platforms/android-21/arch-arm

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

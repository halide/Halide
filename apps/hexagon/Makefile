include ../support/Makefile.inc

CXX-host ?= $(CXX)
CXX-arm-64-android ?= $(ANDROID_NDK_HOME)/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-c++
CXX-arm-32-android ?= $(ANDROID_NDK_HOME)/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-c++

CXXFLAGS-host ?= -lpthread -ldl -lm
CXXFLAGS-arm-64-android ?= --sysroot $(ANDROID_NDK_HOME)/platforms/android-21/arch-arm64 -llog -fPIE -pie
CXXFLAGS-arm-32-android ?= --sysroot $(ANDROID_NDK_HOME)/platforms/android-21/arch-arm -llog -fPIE -pie

all: process-host process-arm-64-android process-arm-32-android

scale: scale.cpp
	$(CXX) $(CXXFLAGS) scale.cpp -g $(LIB_HALIDE) -o scale \
	$(LDFLAGS) $(LLVM_SHARED_LIBS)

scale-%.o: scale
	HL_TARGET=$*-debug-hvx_64 ./scale $*

process-%: process.cpp scale-%.o
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -Wall -O3 process.cpp scale-$*.o -o process-$* -ldl -lpthread

clean:
	rm -f process-* scale-*.o scale

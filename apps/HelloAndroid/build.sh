#!/bin/bash
set -e
android update project -p . --target android-17
mkdir -p bin
c++ jni/hello_generator.cpp ../../tools/GenGen.cpp \
    -g -fno-rtti -Wall -std=c++11 \
    -I ../../include -I ../../build/include \
    -L ../../bin -lHalide -ldl -lpthread -lz \
    -o bin/hello_generator

# 64-bit MIPS (mips-64-android,mips64) currently does not build since
# llvm will not compile for the R6 version of the ISA without Nan2008
# and the gcc toolchain used by the Android build setup requires those
# two options together.
for archs in arm-32-android,armeabi arm-32-android-armv7s,armeabi-v7a arm-64-android,arm64-v8a mips-32-android,mips x86-64-android-sse41,x86_64 x86-32-android,x86 ; do
    IFS=,
    set $archs
    HL_TARGET=$1
    ANDROID_ABI=$2
    mkdir -p bin/$ANDROID_ABI
    ./bin/hello_generator -g hello -o bin/$ANDROID_ABI target=$HL_TARGET
    unset IFS
done

pwd
ndk-build NDK_GEN_OUT=./bin/gen NDK_LIBS_OUT=./bin/lib NDK_OUT=./bin/obj
ant debug
adb install -r bin/HelloAndroid-debug.apk
adb logcat

#!/bin/bash
set -e
mkdir -p bin
# android update project -p . --target android-17
c++ jni/halide.cpp -g -Wall -std=c++11 -L ../../bin -lHalide -I ../../include -I ../../build/include -ldl -lpthread -lz -o bin/a.out

# 64-bit MIPS (mips-64-android,mips64) currently does not build since
# llvm will not compile for the R6 version of the ISA without Nan2008
# and the gcc toolchain used by the Android build setup requires those
# two options together.
for archs in arm-32-android,armeabi arm-32-android-armv7s,armeabi-v7a arm-64-android,arm64-v8a mips-32-android,mips x86-64-android-sse41,x86_64 x86-32-android,x86 ; do
    IFS=,
    set $archs
    hl_target=$1
    android_abi=$2
    mkdir -p bin/halide_generated_$android_abi
    cd bin/halide_generated_$android_abi
    HL_TARGET=$hl_target DYLD_LIBRARY_PATH=../../../../bin LD_LIBRARY_PATH=../../../../bin ../a.out
    cd ../..
    unset IFS
done

pwd
ndk-build NDK_GEN_OUT=./bin/gen NDK_LIBS_OUT=./bin/lib NDK_OUT=./bin/obj
ant debug
adb install -r bin/HelloAndroid-debug.apk
adb logcat

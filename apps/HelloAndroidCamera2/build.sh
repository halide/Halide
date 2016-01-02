#!/bin/bash
set -e
android update project -p . --subprojects --target android-21
if [ -z "$ANDROID_NDK_HOME" ]; then
    echo "Set ANDROID_NDK_HOME to point to your android ndk root directory"
    exit 1
fi
cd jni
# Compile the program that when executed, creates an architecture-specific .so.
# GenGen.cpp is a stub main().
c++ -g -Wall -std=c++11 edge_detect_generator.cpp deinterleave_generator.cpp ../../../tools/GenGen.cpp -L ../../../bin -L ../../../build/lib -lHalide -I ../../../include -I ../../../build/include -ldl -lpthread -lz -fno-rtti

# 64-bit MIPS (mips-64-android,mips64) currently does not build since
# llvm will not compile for the R6 version of the ISA without Nan2008
# and the gcc toolchain used by the Android build setup requires those
# two options together.
for arch in arm-32-android,armeabi arm-32-android-armv7s,armeabi-v7a arm-64-android,arm64-v8a mips-32-android,mips x86-64-android-sse41,x86_64 x86-32-android,x86 ; do
    # IFS is bash's internal field separator. Set it to comma to split arch.
    IFS=,
    set $arch
    echo "Generating for $arch..."
    hl_target=$1
    android_abi=$2
    mkdir -p halide_generated_$android_abi
    cd halide_generated_$android_abi
    # Set the target architecture and run a.out to create the arch-specific # .so.
    DYLD_LIBRARY_PATH=../../../../bin LD_LIBRARY_PATH=../../../../bin ../a.out -g deinterleave -o . target=$hl_target
    DYLD_LIBRARY_PATH=../../../../bin LD_LIBRARY_PATH=../../../../bin ../a.out -g edge_detect -o . target=$hl_target
    cd ..
    unset IFS
done

cd ..
pwd
${ANDROID_NDK_HOME}/ndk-build -C jni # NDK_LOG=1
ant debug
adb install -r bin/HelloAndroidCamera2-debug.apk
adb shell am start com.example.helloandroidcamera2/com.example.helloandroidcamera2.CameraActivity
adb logcat

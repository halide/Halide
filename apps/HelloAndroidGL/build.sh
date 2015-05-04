#!/bin/bash
set -e
if [ -z $HALIDE_ANDROID_SDK_VERSION ]; then
    HALIDE_ANDROID_SDK_VERSION="android-17"
fi
android update project -p . --target $HALIDE_ANDROID_SDK_VERSION
cd jni
c++ -std=c++11 halide_gl_filter.cpp -L ../../../bin -lHalide -I ../../../include -ldl -lpthread -lz
HL_TARGET=arm-32-android-opengl-debug DYLD_LIBRARY_PATH=../../../bin LD_LIBRARY_PATH=../../../bin ./a.out
cd ..
pwd
ndk-build
ant debug
adb install -r bin/HelloAndroidGL-debug.apk
adb logcat

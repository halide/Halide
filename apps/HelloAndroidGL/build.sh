#!/bin/bash
set -e
android update project -p . --target android-17
cd jni
c++ -std=c++11 halide_gl_filter.cpp -L ../../../bin -lHalide -I ../../../include -ldl -lpthread -lz
HL_TARGET=arm-32-android-opengl-debug DYLD_LIBRARY_PATH=../../../bin LD_LIBRARY_PATH=../../../bin ./a.out
cd ..
pwd
ndk-build
ant debug
adb install -r bin/HelloAndroidGL-debug.apk
adb logcat

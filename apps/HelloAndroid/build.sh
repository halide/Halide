#!/bin/bash
set -e
android update project -p .
cd jni
c++ halide.cpp -L ../../../bin -lHalide -I ../../../include -ldl -lpthread -lz
HL_TARGET=arm-32-android DYLD_LIBRARY_PATH=../../../bin LD_LIBRARY_PATH=../../../bin ./a.out
cd ..
pwd
ndk-build
cp jni/*.so libs/armeabi-v7a/
ant debug
adb install -r bin/HelloAndroid-debug.apk
adb logcat

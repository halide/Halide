#!/bin/bash
set -e
android update project -p .
cd jni 
c++ halide.cpp -L ../../../bin -lHalide -I ../../../include -ldl -lpthread 
DYLD_LIBRARY_PATH=../../../bin LD_LIBRARY_PATH=../../../bin HL_TARGET=arm-32-android ./a.out
cd .. 
pwd 
ndk-build 
ant debug 
adb install -r bin/HelloAndroid-debug.apk 
adb logcat

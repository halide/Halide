#!/bin/bash
set -e
android update project -p . --target android-21
make jni-libs
ant debug
adb install -r bin/HelloHalideOpenGLCompute-debug.apk
adb logcat -c
adb shell am start -n com.example.hellohalideopenglcompute/.HalideOpenGLComputeActivity
adb logcat | grep "^I/oglc"

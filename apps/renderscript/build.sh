#!/bin/bash
set -e
android update project -p . --target android-21
make deploy
ndk-build
ant debug
adb install -r bin/HelloHalideRenderscript-debug.apk
adb shell am start -n com.example.hellohaliderenderscript/.HalideRenderscriptActivity
adb logcat | grep "^I/rstest"

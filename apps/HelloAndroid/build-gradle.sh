#!/bin/bash

# Gradle needs to know where the NDK is.
# The easiest way is to set the ANDROID_NDK_HOME environment variable.
# Otherwise, set ndk.dir in local.properties (even though the file itself says
# that it's only used by ant).
# However, if you run "android update" (say, via build.sh), this variable will
# be clobbered.
./gradlew build && adb install -r build/outputs/apk/HelloAndroid-debug.apk && adb shell am start com.example.hellohalide/com.example.hellohalide.CameraActivity

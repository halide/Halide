# TODO(aam): Confirm that application builds and runs for all supported targets:
# APP_ABI := armeabi armeabi-v7a arm64-v8a x86_64 x86
APP_ABI := armeabi-v7a
APP_PLATFORM := android-17

APP_STL := c++_static
LOCAL_C_INCLUDES += ${ANDROID_NDK}/sources/cxx-stl/gnu-libstdc++/4.8/include

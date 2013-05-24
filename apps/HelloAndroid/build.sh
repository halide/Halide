android update project -p .
cd jni && \
c++ halide.cpp -L ../../../bin -lHalide -I ../../../include -ldl -lpthread &&  \
LD_LIBRARY_PATH=../../../bin HL_TARGET=arm-android ./a.out &&  \
cd .. &&  \
~/android-ndk-r8d/ndk-build && \
ant debug &&  \
adb install -r bin/HelloAndroid-debug.apk && \
adb logcat
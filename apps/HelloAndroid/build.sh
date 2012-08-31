cd jni && \
c++ halide.cpp -L ../../../cpp_bindings/ -lHalide -I ../../../cpp_bindings/ &&  \
HL_TARGET=android ./a.out &&  \
llc halide.bc -O3 -mattr=+neon -o halide.s && 
cd .. &&  \
~/android-ndk-r8b/ndk-build && \
ant debug &&  \
adb install -r bin/HelloAndroid-debug.apk && \
adb logcat
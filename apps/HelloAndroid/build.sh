cd jni && \
c++ halide.cpp -L ~/projects/Halide/cpp_bindings/ -lHalide -I ~/projects/Halide/cpp_bindings/ &&  \
HL_TARGET=android ./a.out &&  \
llc halide.bc -mattr=+neon -o halide.s && 
cd .. &&  \
~/android-ndk-r8b/ndk-build && \
ant debug &&  \
adb install -r bin/HelloAndroid-debug.apk && \
adb logcat
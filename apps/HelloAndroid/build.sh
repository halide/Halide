android update project -p .
cd jni && \
c++ halide.cpp -L ../../../cpp_bindings/ -lHalide -I ../../../cpp_bindings/ -ldl -lpthread &&  \
HL_DISABLE_BOUNDS_CHECKING=1 ./a.out &&  \
cat halide.bc | opt -O3 | llc -O3 -mattr=+neon -o halide.s && 
cd .. &&  \
~/android-ndk-r8b/ndk-build && \
ant debug &&  \
adb install -r bin/HelloAndroid-debug.apk && \
adb logcat
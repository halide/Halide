.PHONY: clean

COMMON_FLAGS = -I ${HEX_SDK_ROOT}inc/stddef
COMMON_CCFLAGS = ${COMMON_FLAGS} -fPIC -O3 -I ${HEX_SDK_ROOT}lib/common/remote/ship/android_Release
QAICFLAGS = ${COMMON_FLAGS}

CC-v60 = ${HEX_TOOLS_ROOT}Tools/bin/hexagon-clang
CXX-v60 = ${HEX_TOOLS_ROOT}Tools/bin/hexagon-clang++

CC-arm-64-android = ${ANDROID_NDK_ROOT}/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-gcc
#CXX-arm-64-android = ${ANDROID_NDK_ROOT}/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-g++
CCFLAGS-arm-64-android = --sysroot ${ANDROID_NDK_ROOT}/platforms/android-21/arch-arm64 -llog -fPIE -pie
#CXXFLAGS-arm-64-android=

CC-arm-32-android = ${ANDROID_NDK_ROOT}/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-gcc
#CXX-arm-32-android = ${ANDROID_NDK_ROOT}/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-g++
CCFLAGS-arm-32-android = --sysroot ${ANDROID_NDK_ROOT}/platforms/android-21/arch-arm -llog -fPIE -pie
#CXXFLAGS-arm-32-android=

CCFLAGS-v60 := $(CCFLAGS-v60) ${COMMON_CCFLAGS}
CCFLAGS-arm-64-android := $(CCFLAGS-arm-64-android) ${COMMON_CCFLAGS}
CCFLAGS-arm-32-android := $(CCFLAGS-arm-32-android) ${COMMON_CCFLAGS}

halide_hexagon_remote.h: halide_hexagon_remote.idl
	$(QAIC) $(QAICFLAGS) $^

halide_hexagon_remote_skel.c: halide_hexagon_remote.idl
	$(QAIC) $(QAICFLAGS) $^

halide_hexagon_remote_stub.c: halide_hexagon_remote.idl
	$(QAIC) $(QAICFLAGS) $^

halide_hexagon_remote_skel-%.o: halide_hexagon_remote_skel.c
	$(CC-$*) $(CCFLAGS-$*) -c $^ -o $@

libhalide_hexagon_remote-%.so: halide_hexagon_remote.cpp halide_hexagon_remote_skel-%.o
	$(CXX-$*) $(CCFLAGS-$*) $(CXXFLAGS-$*) $^ -shared -o $@

libhalide_hexagon_host-%.so: halide_hexagon_remote_stub.c
	$(CC-$*) $(CCFLAGS-$*) $^ -shared -o $@

clean:
	rm -rf halide_hexagon_remote.h halide_hexagon_remote_skel.c halide_hexagon_remote_stub.c
	rm -rf libhalide_hexagon_host-arm-32-android.so
	rm -rf libhalide_hexagon_host-arm-64-android.so
	rm -rf libhalide_hexagon_remote-v60.so

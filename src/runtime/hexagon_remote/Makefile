.PHONY: clean

COMMON_FLAGS = -I ${HEX_SDK_ROOT}inc/stddef
COMMON_CCFLAGS = ${COMMON_FLAGS} -fPIC -O3 -I ${HEX_SDK_ROOT}lib/common/remote/ship/android_Release
QAICFLAGS = ${COMMON_FLAGS}

CC-v60 = ${HEX_TOOLS_ROOT}Tools/bin/hexagon-clang
CXX-v60 = ${HEX_TOOLS_ROOT}Tools/bin/hexagon-clang++

CC-arm-64-android = ${ANDROID_NDK_ROOT}/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-gcc
#CXX-arm-64-android = ${ANDROID_NDK_ROOT}/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-g++
CCFLAGS-arm-64-android = --sysroot ${ANDROID_NDK_ROOT}/platforms/android-21/arch-arm64
#CXXFLAGS-arm-64-android=

CC-arm-32-android = ${ANDROID_NDK_ROOT}/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-gcc
#CXX-arm-32-android = ${ANDROID_NDK_ROOT}/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-g++
CCFLAGS-arm-32-android = --sysroot ${ANDROID_NDK_ROOT}/platforms/android-21/arch-arm
#CXXFLAGS-arm-32-android=

CCFLAGS-v60 := $(CCFLAGS-v60) ${COMMON_CCFLAGS} -I ${HEX_SDK_ROOT}inc
CCFLAGS-arm-64-android := $(CCFLAGS-arm-64-android) ${COMMON_CCFLAGS} -llog -fPIE -pie \
	-L${HEX_SDK_ROOT}lib/common/remote/ship/android_Release_aarch64/ -ladsprpc \
	${HEX_SDK_ROOT}lib/common/adspmsgd/ship/android_Release_aarch64/adspmsgd.a
CCFLAGS-arm-32-android := $(CCFLAGS-arm-32-android) ${COMMON_CCFLAGS} -llog -fPIE -pie \
	-L${HEX_SDK_ROOT}lib/common/remote/ship/android_Release/ -ladsprpc \
	${HEX_SDK_ROOT}lib/common/adspmsgd/ship/android_Release/adspmsgd.a

halide_hexagon_remote.h: halide_hexagon_remote.idl
	$(QAIC) $(QAICFLAGS) $^

halide_hexagon_remote_skel.c: halide_hexagon_remote.idl
	$(QAIC) $(QAICFLAGS) $^

halide_hexagon_remote_stub.c: halide_hexagon_remote.idl
	$(QAIC) $(QAICFLAGS) $^

%/halide_hexagon_remote_skel.o: halide_hexagon_remote_skel.c
	mkdir -p $*
	$(CC-$*) $(CCFLAGS-$*) -c $^ -o $@

%/halide_hexagon_remote.o: halide_hexagon_remote.cpp elf.h
	mkdir -p $*
	$(CXX-$*) $(CCFLAGS-$*) -c halide_hexagon_remote.cpp -o $@

%/libhalide_hexagon_remote_skel.so: %/halide_hexagon_remote.o %/halide_hexagon_remote_skel.o
	mkdir -p $*
	$(CC-$*) -mv5 -O0 -mG0lib -G0 -fpic -shared -Wl,-Bsymbolic -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=free -Wl,--wrap=realloc -Wl,--wrap=memalign -Wl,--wrap=__stack_chk_fail -lc -Wl,-Map=halide_hexagon_remote.so.map -Wl,-soname=libhalide_hexagon_remote_skel.so   -o $*/libhalide_hexagon_remote_skel.so -Wl,--start-group  $*/halide_hexagon_remote.o $*/halide_hexagon_remote_skel.o -Wl,--end-group

%/libhalide_hexagon_host.so: halide_hexagon_remote_stub.c
	mkdir -p $*
	$(CC-$*) $(CCFLAGS-$*) $^ -shared -o $@

clean:
	rm -rf halide_hexagon_remote.h halide_hexagon_remote_skel.c halide_hexagon_remote_stub.c
	rm -rf arm-32-android/
	rm -rf arm-64-android/
	rm -rf v60/

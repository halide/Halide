.PHONY: clean

COMMON_FLAGS = -I ${HEX_SDK_ROOT}inc/stddef
COMMON_CCFLAGS = ${COMMON_FLAGS} -O3 -I ${HEX_SDK_ROOT}lib/common/remote/ship/android_Release
QAICFLAGS = ${COMMON_FLAGS}

CC-host = ${CXX}
CXX-host = ${CXX}

CC-v60 = ${HEX_TOOLS_ROOT}Tools/bin/hexagon-clang
CXX-v60 = ${HEX_TOOLS_ROOT}Tools/bin/hexagon-clang++

LD-v60 = $(HEX_TOOLS_ROOT)Tools/bin/hexagon-link

CC-arm-64-android = ${ANDROID_NDK_ROOT}/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-gcc
#CXX-arm-64-android = ${ANDROID_NDK_ROOT}/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-g++
CCFLAGS-arm-64-android = --sysroot ${ANDROID_NDK_ROOT}/platforms/android-21/arch-arm64
#CXXFLAGS-arm-64-android=

CC-arm-32-android = ${ANDROID_NDK_ROOT}/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-gcc
#CXX-arm-32-android = ${ANDROID_NDK_ROOT}/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-g++
CCFLAGS-arm-32-android = --sysroot ${ANDROID_NDK_ROOT}/platforms/android-21/arch-arm
#CXXFLAGS-arm-32-android=

CCFLAGS-host := ${CCFLAGS} -I ${HEX_TOOLS_ROOT}Tools/include/iss/ -fPIC \
	-L${HEX_TOOLS_ROOT}Tools/lib/iss/ -Wl,-rpath,${HEX_TOOLS_ROOT}Tools/lib/iss -lwrapper

CCFLAGS-v60 := $(CCFLAGS-v60) ${COMMON_CCFLAGS} -I ${HEX_SDK_ROOT}inc -I ${HEX_SDK_ROOT}lib/common/qurt/ADSPv60MP/include

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
	$(CC-$*) $(CCFLAGS-$*) -fPIC -c $^ -o $@

%/remote.o: remote.cpp
	mkdir -p $*
	$(CXX-$*) $(CCFLAGS-$*) -fPIC -c remote.cpp -o $@

# Build rules for the hexagon implementation.
%/libhalide_hexagon_remote_skel.so: %/remote.o %/halide_hexagon_remote_skel.o
	mkdir -p $*
	$(CC-$*) -m$* -O0 -mG0lib -G0 -fpic -shared -Wl,-Bsymbolic -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=free -Wl,--wrap=realloc -Wl,--wrap=memalign -Wl,--wrap=__stack_chk_fail -lc -Wl,-Map=halide_hexagon_remote.so.map -Wl,-soname=libhalide_hexagon_remote_skel.so   -o $*/libhalide_hexagon_remote_skel.so -Wl,--start-group  $*/remote.o $*/halide_hexagon_remote_skel.o -Wl,--end-group

%/libhalide_hexagon_host.so: halide_hexagon_remote_stub.c
	mkdir -p $*
	$(CC-$*) $(CCFLAGS-$*) $^ -shared -o $@

# Build rules for the simulator implementation.
%/sim_remote.o: sim_remote.cpp sim_protocol.h
	mkdir -p $*
	$(CXX-$*) $(CCFLAGS-$*) -c sim_remote.cpp -o $@

%/sim_host.o: sim_host.cpp sim_protocol.h
	mkdir -p $*
	$(CXX-$*) -std=c++11 $(CCFLAGS-$*) -c sim_host.cpp -o $@


CRT0_STANDALONE=$(shell $(CXX-v60) -G0 -print-file-name=crt0_standalone.o)
CRT0           =$(shell $(CXX-v60) -G0 -print-file-name=crt0.o)
INIT           =$(shell $(CXX-v60) -G0 -print-file-name=init.o)
LIB_STANDALONE =$(shell $(CXX-v60) -G0 -print-file-name=libstandalone.a)
LIB_C          =$(shell $(CXX-v60) -G0 -print-file-name=libc.a)
LIB_GCC        =$(shell $(CXX-v60) -G0 -print-file-name=libgcc.a)
FINI           =$(shell $(CXX-v60) -G0 -print-file-name=fini.o)
LIBDL          =$(HEX_TOOLS_ROOT)Tools/target/hexagon/lib/v60/G0/libdl.a

%/hexagon_sim_remote: %/sim_remote.o
	mkdir -p $*
	$(LD-$*) -o $@ $(CRT0_STANDALONE) $(CRT0) $(INIT) $*/sim_remote.o $(LIBDL) --start-group  $(LIB_STANDALONE) --whole-archive $(LIB_C) --no-whole-archive $(LIB_GCC) --end-group $(FINI) --dynamic-linker= -E --force-dynamic

#%/hexagon_sim_remote: %/sim_remote.o
#	mkdir -p $*
#	$(CXX-$*) -m$* -G0 -mG0lib $^ $(CCFLAGS-$*) -o $@ -lhexagon -ldl

host/libhalide_hexagon_host.so: host/sim_host.o
	mkdir -p host
	$(CC-host) $^ $(CCFLAGS-host) -shared -o $@

clean:
	rm -rf halide_hexagon_remote.h halide_hexagon_remote_skel.c halide_hexagon_remote_stub.c
	rm -rf arm-32-android/
	rm -rf arm-64-android/
	rm -rf v60/
	rm -rf host/

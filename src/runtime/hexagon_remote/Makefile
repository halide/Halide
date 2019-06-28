#
# This Makefile creates some pre-built glue code that is kept (in binary form)
# in the Halide repo; this is intended to allow end users to compile for
# Hexagon targets without requiring the Hexagon SDK to be present on the build
# system. (The Hexagon SDK is of course required to build the glue code itself.)
# At present, only Linux systems are supported for building and testing; this
# could probably be made to work on Windows (via MinGW) with a little effort.
#
# In order to build this, you'll need to ensure that the following
# env vars are set correctly:
#
# HEXAGON_SDK_ROOT   : path to Qualcom Hexagon SDK
# HEXAGON_TOOLS_ROOT : path to Qualcomm Hexagon Tools
# HEXAGON_QAIC       : path to Qualcomm qaic compiler
#
# Note that all build products are put in a subdirectory of a local bin/
# directory (even machine-generated C++ files), to make clear which files
# are "real" source vs machine-generated. This is a rare case in which
# a bin/ folder is meant to be kept in source control.
#
# Note that -- despite the efforts above -- the Hexagon SDK *is* required
# in order to *use* the simulator (which requires proprietary binary blobs
# from the SDK that cannot be redistributed with Halide).
#

HEXAGON_SDK_ROOT ?= ${HOME}/Qualcomm/Hexagon_SDK/3.3.3
HEXAGON_TOOLS_ROOT ?= ${HEXAGON_SDK_ROOT}/tools/HEXAGON_Tools/8.1.05

ANDROID_NDK_ROOT ?= ${HEXAGON_SDK_ROOT}/tools/android-ndk-r10d

# QAIC compiler may vary depending on your build env.
HEXAGON_QAIC ?= ${HEXAGON_SDK_ROOT}/tools/qaic/Ubuntu14/qaic
HEXAGON_ELFSIGNER ?= ${HEXAGON_SDK_ROOT}/tools/elfsigner/elfsigner.py

# Some SDK versions use "inc/", some use "incs/"
HEXAGON_SDK_INCLUDES ?= "${HEXAGON_SDK_ROOT}/incs"

# Some SDK versions use "lib/", some use "libs/"
HEXAGON_SDK_LIBS ?= "${HEXAGON_SDK_ROOT}/libs"

COMMON_FLAGS = -I ${HEXAGON_SDK_INCLUDES}/stddef -I../
COMMON_CCFLAGS = ${COMMON_FLAGS} -O3 -I ${HEXAGON_SDK_LIBS}/common/remote/ship/android_Release
HEXAGON_QAICFLAGS = ${COMMON_FLAGS}

BIN ?= bin

CC-host = ${CXX}
CXX-host = ${CXX}

CC-v60 = ${HEXAGON_TOOLS_ROOT}/Tools/bin/hexagon-clang
CXX-v60 = ${HEXAGON_TOOLS_ROOT}/Tools/bin/hexagon-clang++
LD-v60 = ${HEXAGON_TOOLS_ROOT}/Tools/bin/hexagon-link

CC-arm-64-android = ${ANDROID_NDK_ROOT}/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-gcc
CXX-arm-64-android = ${ANDROID_NDK_ROOT}/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-g++
CCFLAGS-arm-64-android = --sysroot ${ANDROID_NDK_ROOT}/platforms/android-21/arch-arm64

CC-arm-32-android = ${ANDROID_NDK_ROOT}/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-gcc
CXX-arm-32-android = ${ANDROID_NDK_ROOT}/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-g++
CCFLAGS-arm-32-android = --sysroot ${ANDROID_NDK_ROOT}/platforms/android-21/arch-arm

CCFLAGS-host := ${CCFLAGS} -I../ -I ${HEXAGON_TOOLS_ROOT}/Tools/include/iss/ -fPIC \
	-L${HEXAGON_TOOLS_ROOT}/Tools/lib/iss/ -lwrapper

CCFLAGS-v60 := $(CCFLAGS-v60) -I ${HEXAGON_SDK_LIBS}/common/rtld/ship/hexagon_Release_toolv80_v62 ${COMMON_CCFLAGS} -I ${HEXAGON_SDK_INCLUDES} -I ${HEXAGON_SDK_LIBS}/common/qurt/ADSPv60MP/include

CCFLAGS-arm-64-android := $(CCFLAGS-arm-64-android) ${COMMON_CCFLAGS} -llog -fPIE -pie
CCFLAGS-arm-32-android := $(CCFLAGS-arm-32-android) ${COMMON_CCFLAGS} -llog -fPIE -pie

AR-v60 = ${HEXAGON_TOOLS_ROOT}/Tools/bin/hexagon-ar

.PHONY: all
all: hosts remotes

.PHONY: hosts
hosts: \
	$(BIN)/arm-64-android/libhalide_hexagon_host.so \
	$(BIN)/arm-32-android/libhalide_hexagon_host.so \
	$(BIN)/host/libhalide_hexagon_host.so

.PHONY: remotes
remotes: \
	$(BIN)/v60/libhalide_hexagon_remote_skel.so \
	$(BIN)/v60/signed_by_debug/libhalide_hexagon_remote_skel.so \
	$(BIN)/v60/hexagon_sim_remote \
	$(BIN)/v60/libsim_qurt.a \
	$(BIN)/v60/libsim_qurt_vtcm.a

$(BIN)/src/halide_hexagon_remote.h $(BIN)/src/halide_hexagon_remote_skel.c $(BIN)/src/halide_hexagon_remote_stub.c: halide_hexagon_remote.idl
	mkdir -p $(@D)
	$(HEXAGON_QAIC) $(HEXAGON_QAICFLAGS) $^ -o $(@D)

$(BIN)/%/halide_hexagon_remote_skel.o: $(BIN)/src/halide_hexagon_remote_skel.c
	mkdir -p $(@D)
	$(CC-$*) $(CCFLAGS-$*) -fPIC -c $^ -o $@

$(BIN)/%/thread_pool.o: thread_pool.cpp
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -fPIC -c thread_pool.cpp -o $@

$(BIN)/%/halide_remote.o: halide_remote.cpp dlib.h known_symbols.h $(BIN)/src/halide_hexagon_remote.h
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -I$(BIN)/src/ -fPIC -c halide_remote.cpp -o $@

$(BIN)/%/dlib.o: dlib.cpp dlib.h
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -fPIC -c dlib.cpp -o $@

$(BIN)/%/host_malloc.o: host_malloc.cpp
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -fPIC -c host_malloc.cpp -o $@

$(BIN)/%/libadsprpc_shim.o: libadsprpc_shim.cpp
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -fPIC -c libadsprpc_shim.cpp -o $@

$(BIN)/%/host_shim.o: host_shim.cpp $(BIN)/src/halide_hexagon_remote.h
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -I$(BIN)/src/ -fPIC -c host_shim.cpp -o $@

$(BIN)/%/known_symbols.o: known_symbols.cpp
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -fPIC -c known_symbols.cpp -o $@

$(BIN)/%/nearbyint.o: nearbyint.cpp
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -fPIC -c nearbyint.cpp -o $@

$(BIN)/%/c11_stubs.o: c11_stubs.cpp
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -fPIC -c c11_stubs.cpp -o $@

$(BIN)/%/log.o: log.cpp
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -fPIC -c log.cpp -o $@

# Build rules for the hexagon implementation.
$(BIN)/%/libhalide_hexagon_remote_skel.so: $(BIN)/%/halide_remote.o $(BIN)/%/halide_hexagon_remote_skel.o $(BIN)/%/nearbyint.o $(BIN)/%/c11_stubs.o $(BIN)/%/log.o $(BIN)/%/dlib.o $(BIN)/%/known_symbols.o
	mkdir -p $(@D)
	$(CC-$*) -m$* -mG0lib -G0 -fpic -shared -lc $^ -Wl,-soname=libhalide_hexagon_remote_skel.so -Wl,--no-threads -o $@ \
	-Wl,-Bsymbolic -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=free \
	-Wl,--wrap=realloc -Wl,--wrap=memalign -Wl,--wrap=__stack_chk_fail

$(BIN)/%/signed_by_debug/libhalide_hexagon_remote_skel.so: $(BIN)/%/libhalide_hexagon_remote_skel.so
	mkdir -p $(@D)
	python $(HEXAGON_ELFSIGNER) --no_disclaimer -i $^ -o `dirname $@`

$(BIN)/%/libhalide_hexagon_host.so: $(BIN)/src/halide_hexagon_remote_stub.c $(BIN)/%/host_malloc.o $(BIN)/%/host_shim.o $(BIN)/%/libadsprpc_shim.o
	mkdir -p $(@D)
	$(CC-$*) $^ $(CCFLAGS-$*) -Wl,-soname,libhalide_hexagon_host.so -shared -o $@

# Build rules for the simulator implementation.
$(BIN)/%/sim_remote.o: sim_remote.cpp sim_protocol.h dlib.h known_symbols.h $(BIN)/src/halide_hexagon_remote.h
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -I$(BIN)/src/ -c sim_remote.cpp -o $@

$(BIN)/%/sim_host.o: sim_host.cpp sim_protocol.h
	mkdir -p $(@D)
	$(CXX-$*) -std=c++11 $(CCFLAGS-$*) -c sim_host.cpp -o $@

$(BIN)/%/sim_qurt.o: sim_qurt.cpp
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -c sim_qurt.cpp -o $@

$(BIN)/%/libsim_qurt.a: $(BIN)/%/sim_qurt.o
	mkdir -p $(@D)
	$(AR-$*) rcs $@ $^

$(BIN)/%/sim_qurt_vtcm.o: sim_qurt_vtcm.cpp
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -c sim_qurt_vtcm.cpp -o $@

$(BIN)/%/libsim_qurt_vtcm.a: $(BIN)/%/sim_qurt_vtcm.o
	mkdir -p $(@D)
	$(AR-$*) rcs $@ $^

CRT0_STANDALONE=$(shell $(CXX-v60) -G0 -print-file-name=crt0_standalone.o)
CRT0           =$(shell $(CXX-v60) -G0 -print-file-name=crt0.o)
INIT           =$(shell $(CXX-v60) -G0 -print-file-name=init.o)
LIB_STANDALONE =$(shell $(CXX-v60) -G0 -print-file-name=libstandalone.a)
LIB_C          =$(shell $(CXX-v60) -G0 -print-file-name=libc.a)
LIB_GCC        =$(shell $(CXX-v60) -G0 -print-file-name=libgcc.a)
FINI           =$(shell $(CXX-v60) -G0 -print-file-name=fini.o)
LIBDL          =$(HEXAGON_TOOLS_ROOT)/Tools/target/hexagon/lib/v60/G0/libdl.a

$(BIN)/%/hexagon_sim_remote: $(BIN)/%/sim_remote.o $(BIN)/%/dlib.o $(BIN)/%/known_symbols.o $(BIN)/%/libsim_qurt.a $(BIN)/%/libsim_qurt_vtcm.a
	mkdir -p $(@D)
	$(LD-$*) -o $@ $(CRT0_STANDALONE) $(CRT0) $(INIT) $(BIN)/$*/sim_remote.o $(BIN)/$*/dlib.o $(BIN)/$*/known_symbols.o $(BIN)/$*/libsim_qurt.a $(LIBDL) \
	--start-group  $(LIB_STANDALONE) --whole-archive $(LIB_C) $(BIN)/$*/libsim_qurt_vtcm.a --no-whole-archive $(LIB_GCC) --end-group $(FINI) \
	--dynamic-linker= -E --force-dynamic

$(BIN)/host/libhalide_hexagon_host.so: $(BIN)/host/sim_host.o
	mkdir -p $(@D)
	$(CC-host) $^ $(CCFLAGS-host) -Wl,-soname,libhalide_hexagon_host.so -shared -o $@

.PHONY: clean
clean:
	rm -rf $(BIN)/

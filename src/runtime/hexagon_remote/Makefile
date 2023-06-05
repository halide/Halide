#
# This Makefile creates some pre-built glue code that is kept (in binary form)
# in the Halide repo; this is intended to allow end users to compile for
# Hexagon targets without requiring the Hexagon SDK to be present on the build
# system. (The Hexagon SDK is of course required to build the glue code itself.)
# At present, only Linux systems are supported for building and testing.
#
# In order to build this, you'll need to ensure that the following
# env vars are set correctly:
#
# SDK_ROOT           : path to Qualcomm SDK storage
# HEXAGON_SDK_ROOT   : path to Qualcomm Hexagon SDK
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

SDK_ROOT ?= ${HOME}/Qualcomm
HEXAGON_SDK_ROOT ?= ${SDK_ROOT}/Hexagon_SDK/4.3.0.0
# Devices >sm8350 will only run skel shared objects that have been built with
# SDK version 4.2.02 or newer.
HEXAGON_TOOLS_VER ?= 8.4.11
# Need Hexagon tools >= 8.4.05 for running simulator-offload test for hvx_v68
# Need Hexagon tools >= 8.3.09 for running simulator-offload test for hvx_v66
HEXAGON_TOOLS_ROOT ?= ${HEXAGON_SDK_ROOT}/tools/HEXAGON_Tools/$(HEXAGON_TOOLS_VER)

ANDROID_NDK_ROOT ?= ${HEXAGON_SDK_ROOT}/tools/android-ndk-r19c

# QAIC compiler may vary depending on your build env.
HEXAGON_QAIC ?= ${HEXAGON_SDK_ROOT}/ipc/fastrpc/qaic/Ubuntu16/qaic
HEXAGON_ELFSIGNER ?= ${HEXAGON_SDK_ROOT}/tools/elfsigner/elfsigner.py

# Some SDK versions use "inc/", some use "incs/"
HEXAGON_SDK_INCLUDES ?= "${HEXAGON_SDK_ROOT}/incs"

# Some SDK versions use "lib/", some use "libs/"
HEXAGON_SDK_LIBS ?= "${HEXAGON_SDK_ROOT}/libs"

# Allow for passing extra preprocessor definitions via env var, e.g.
#
#    make HEXAGON_EXTRA_CCFLAGS="-DENABLE_FOO=1" ...
#
HEXAGON_EXTRA_CCFLAGS ?=

COMMON_FLAGS = -I ${HEXAGON_SDK_INCLUDES}/stddef -I../ -I ${HEXAGON_SDK_INCLUDES}
COMMON_CCFLAGS = ${COMMON_FLAGS} -O3 -I ${HEXAGON_SDK_LIBS}/common/remote/ship/android_Release -I ${HEXAGON_SDK_ROOT}/libs/qprintf/inc ${HEXAGON_EXTRA_CCFLAGS}
HEXAGON_QAICFLAGS = ${COMMON_FLAGS}

BIN ?= bin

CC-host = ${CXX}
CXX-host = ${CXX}

CC-v65  = ${HEXAGON_TOOLS_ROOT}/Tools/bin/hexagon-clang
CXX-v65 = ${HEXAGON_TOOLS_ROOT}/Tools/bin/hexagon-clang++
LD-v65  = ${HEXAGON_TOOLS_ROOT}/Tools/bin/hexagon-link

CC-arm-64-android = ${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang
CXX-arm-64-android = ${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++
CCFLAGS-arm-64-android = -target aarch64-linux-android21

CC-arm-32-android = ${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang
CXX-arm-32-android = ${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++
CCFLAGS-arm-32-android = -target armv7a-linux-androideabi21

CCFLAGS-host := ${CCFLAGS} -I../ -I ${HEXAGON_TOOLS_ROOT}/Tools/include/iss/ -fPIC \
	-L${HEXAGON_TOOLS_ROOT}/Tools/lib/iss/ -lwrapper

CCFLAGS-v65 := $(CCFLAGS-v65) -I ${HEXAGON_SDK_LIBS}/common/rtld/ship/hexagon_Release_toolv83_v65 ${COMMON_CCFLAGS} -I ${HEXAGON_SDK_INCLUDES} -I ${HEXAGON_SDK_LIBS}/../rtos/qurt/computev65/include/qurt -mhvx -mhvx-length=128B -fno-exceptions -mv65

CCFLAGS-arm-64-android := $(CCFLAGS-arm-64-android) ${COMMON_CCFLAGS} -llog -fPIE -pie -L${HEXAGON_SDK_LIBS}/../ipc/fastrpc/remote/ship/android_aarch64/
CCFLAGS-arm-32-android := $(CCFLAGS-arm-32-android) ${COMMON_CCFLAGS} -llog -fPIE -pie -L${HEXAGON_SDK_LIBS}/../ipc/fastrpc/remote/ship/android

AR-v65 = ${HEXAGON_TOOLS_ROOT}/Tools/bin/hexagon-ar

.PHONY: all
all: hosts remotes

.PHONY: hosts
hosts: \
	$(BIN)/arm-64-android/libhalide_hexagon_host.so \
	$(BIN)/arm-32-android/libhalide_hexagon_host.so \
	$(BIN)/cdsp/arm-64-android/libhalide_hexagon_host.so \
	$(BIN)/cdsp/arm-32-android/libhalide_hexagon_host.so \
	$(BIN)/adsp/arm-64-android/libhalide_hexagon_host.so \
	$(BIN)/adsp/arm-32-android/libhalide_hexagon_host.so \
	$(BIN)/host/libhalide_hexagon_host.so

.PHONY: remotes
remotes: \
	$(BIN)/v65/libhalide_hexagon_remote_skel.so \
	$(BIN)/v65/signed_by_debug/libhalide_hexagon_remote_skel.so \
	$(BIN)/v65/hexagon_sim_remote \
	$(BIN)/v65/libsim_qurt.a


$(BIN)/src/halide_hexagon_remote.h $(BIN)/src/halide_hexagon_remote_skel.c $(BIN)/src/halide_hexagon_remote_stub.c: halide_hexagon_remote.idl
	mkdir -p $(@D)
	$(HEXAGON_QAIC) $(HEXAGON_QAICFLAGS) $^ -o $(@D)

$(BIN)/%/halide_hexagon_remote_skel.o: $(BIN)/src/halide_hexagon_remote_skel.c
	mkdir -p $(@D)
	$(CC-$*) $(CCFLAGS-$*) -fPIC -c $^ -o $@

$(BIN)/%/thread_pool.o: thread_pool.cpp
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -fPIC -c thread_pool.cpp -o $@

$(BIN)/%/halide_remote.o: halide_remote.cpp known_symbols.h $(BIN)/src/halide_hexagon_remote.h
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -I$(BIN)/src/ -fPIC -c halide_remote.cpp -o $@

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
$(BIN)/%/libhalide_hexagon_remote_skel.so: $(BIN)/%/halide_remote.o $(BIN)/%/halide_hexagon_remote_skel.o $(BIN)/%/nearbyint.o $(BIN)/%/c11_stubs.o $(BIN)/%/log.o $(BIN)/%/known_symbols.o
	mkdir -p $(@D)
	$(CC-$*) -m$* -mG0lib -G0 -fpic -shared -lc $^ -Wl,-soname=libhalide_hexagon_remote_skel.so -Wl,--no-threads -o $@ \
	-Wl,-Bsymbolic -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=free \
	-Wl,--wrap=realloc -Wl,--wrap=memalign -Wl,--wrap=__stack_chk_fail -Wl,--whole-archive ${HEXAGON_SDK_ROOT}/libs/qprintf/prebuilt/hexagon_*84_v65/libqprintf.a

$(BIN)/%/signed_by_debug/libhalide_hexagon_remote_skel.so: $(BIN)/%/libhalide_hexagon_remote_skel.so
	mkdir -p $(@D)
	python3 $(HEXAGON_ELFSIGNER) --no_disclaimer -i $^ -o `dirname $@`

$(BIN)/%/libhalide_hexagon_host.so: $(BIN)/src/halide_hexagon_remote_stub.c $(BIN)/%/host_malloc.o $(BIN)/%/host_shim.o $(BIN)/%/libadsprpc_shim.o
	mkdir -p $(@D)
	$(CC-$*) $^ $(CCFLAGS-$*) -Wl,-soname,libhalide_hexagon_host.so -shared -o $@

$(BIN)/cdsp/%/libhalide_hexagon_host.so: bin/src/halide_hexagon_remote_stub.c bin/%/host_malloc.o bin/%/host_shim.o
	mkdir -p $(@D)
	$(CC-$*) $^ $(CCFLAGS-$*) -Wl,-soname,libhalide_hexagon_host.so -shared -o $@  -lcdsprpc

$(BIN)/adsp/%/libhalide_hexagon_host.so: bin/src/halide_hexagon_remote_stub.c bin/%/host_malloc.o bin/%/host_shim.o
	mkdir -p $(@D)
	$(CC-$*) $^ $(CCFLAGS-$*) -Wl,-soname,libhalide_hexagon_host.so -shared -o $@  -ladsprpc

# Build rules for the simulator implementation.
$(BIN)/%/sim_remote.o: sim_remote.cpp sim_protocol.h known_symbols.h $(BIN)/src/halide_hexagon_remote.h
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -I$(BIN)/src/ -c sim_remote.cpp -o $@

$(BIN)/%/sim_host.o: sim_host.cpp sim_protocol.h
	mkdir -p $(@D)
	$(CXX-$*) -std=c++17 $(CCFLAGS-$*) -c sim_host.cpp -o $@

$(BIN)/%/sim_qurt.o: sim_qurt.cpp
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -c sim_qurt.cpp -o $@

$(BIN)/%/sim_qurt_vtcm.o: sim_qurt_vtcm.cpp
	mkdir -p $(@D)
	$(CXX-$*) $(CCFLAGS-$*) -c sim_qurt_vtcm.cpp -o $@

$(BIN)/%/libsim_qurt.a: $(BIN)/%/sim_qurt.o $(BIN)/%/sim_qurt_vtcm.o
	mkdir -p $(@D)
	$(AR-$*) rcs $@ $^

CRT0_STANDALONE=$(shell $(CXX-v65) -mv65 -G0 -print-file-name=crt0_standalone.o)
CRT0           =$(shell $(CXX-v65) -mv65 -G0 -print-file-name=crt0.o)
INIT           =$(shell $(CXX-v65) -mv65 -G0 -print-file-name=init.o)
LIB_STANDALONE =$(shell $(CXX-v65) -mv65 -G0 -print-file-name=libstandalone.a)
LIB_C          =$(shell $(CXX-v65) -mv65 -G0 -print-file-name=libc.a)
LIB_GCC        =$(shell $(CXX-v65) -mv65 -G0 -print-file-name=libgcc.a)
FINI           =$(shell $(CXX-v65) -mv65 -G0 -print-file-name=fini.o)
LIBDL          =$(HEXAGON_TOOLS_ROOT)/Tools/target/hexagon/lib/v65/G0/libdl.a

$(BIN)/%/hexagon_sim_remote: $(BIN)/%/sim_remote.o $(BIN)/%/known_symbols.o $(BIN)/%/libsim_qurt.a
	mkdir -p $(@D)
	$(LD-$*) -mv65 -o $@ $(CRT0_STANDALONE) $(CRT0) $(INIT) $(BIN)/$*/sim_remote.o $(BIN)/$*/known_symbols.o $(LIBDL) \
	--start-group  $(LIB_STANDALONE) --whole-archive $(LIB_C) $(BIN)/$*/libsim_qurt.a --no-whole-archive $(LIB_GCC) --end-group $(FINI) \
	--dynamic-linker= -E --force-dynamic

$(BIN)/host/libhalide_hexagon_host.so: $(BIN)/host/sim_host.o
	mkdir -p $(@D)
	$(CC-host) $^ $(CCFLAGS-host) -Wl,-soname,libhalide_hexagon_host.so -shared -o $@

.PHONY: clean
clean:
	rm -rf $(BIN)/

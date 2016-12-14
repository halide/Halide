CXX-host ?= c++
# Use the build/tools/make-standalone-toolchain.sh script inside the
# android ndk to make a standalone toolchain to use for this app.
CXX-arm-64-android ?= $(ANDROID_ARM64_TOOLCHAIN)/bin/aarch64-linux-android-c++
CXX-arm-32-android ?= $(ANDROID_ARM_TOOLCHAIN)/bin/arm-linux-androideabi-c++
CXX-hexagon-32-qurt-hvx_64 ?= $(HL_HEXAGON_TOOLS)/bin/hexagon-clang++
CXX-hexagon-32-qurt-hvx_128 ?= $(HL_HEXAGON_TOOLS)/bin/hexagon-clang++

CXXFLAGS-arm-64-android ?= -llog -fPIE -pie
CXXFLAGS-arm-32-android ?= -llog -fPIE -pie
CXXFLAGS-hexagon-32-qurt-hvx_64 ?= -mhvx -G0
CXXFLAGS-hexagon-32-qurt-hvx_128 ?= -mhvx-double -G0

LDFLAGS-host ?= -lpthread -ldl
LDFLAGS-hexagon-32-qurt-hvx_64 ?= -L../../tools/sim_qurt -lsim_qurt
LDFLAGS-hexagon-32-qurt-hvx_128 ?= -L../../tools/sim_qurt -lsim_qurt


all: \
	driver-host \
	driver-arm-64-android \
	driver-arm-32-android \
	driver-hexagon-32-qurt-hvx_64 \
	driver-hexagon-32-qurt-hvx_128 \

%/filters.h:
	mkdir -p $*
	make -C ../../ bin/correctness_simd_op_check
	cd $* && HL_TARGET=$* LD_LIBRARY_PATH=../../../bin ../../../bin/correctness_simd_op_check
	cat $*/test_*.h > $*/filter_headers.h
	echo "filter filters[] = {" > $*/filters.h
	cd $*; for f in test_*.h; do n=$${f/.h/}; echo '{"'$${n}'", &'$${n}'},'; done >> filters.h
	echo '{NULL, NULL}};' >> $*/filters.h

driver-%: driver.cpp %/filters.h
	$(CXX-$*) $(CXXFLAGS-$*) -I ../../include -O3 -I $* driver.cpp $*/test_*.o $*/simd_op_check_runtime.o -o driver-$* $(LDFLAGS-$*)

clean:
	rm -rf filters.h filter_headers.h
	rm -rf driver-*
	rm -rf arm-32-android arm-64-android host
	rm -rf hexagon-32-qurt-hvx_64 hexagon-32-qurt-hvx_128
	find . -iname "test_*.h" -type f -exec rm {} +
	find . -iname "check_*.s" -type f -exec rm {} +
	find . -iname "test_*.o" -type f -exec rm {} +

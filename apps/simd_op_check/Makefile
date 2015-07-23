CXX-host ?= c++
CXX-arm-64-android ?= $(ANDROID_NDK_HOME)/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64/bin/aarch64-linux-android-c++
CXX-arm-32-android ?= $(ANDROID_NDK_HOME)/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-c++

CXXFLAGS-host ?= -lpthread
CXXFLAGS-arm-64-android ?= --sysroot $(ANDROID_NDK_HOME)/platforms/android-21/arch-arm64 -llog -fPIE -pie
CXXFLAGS-arm-32-android ?= --sysroot $(ANDROID_NDK_HOME)/platforms/android-21/arch-arm -llog -fPIE -pie

all: driver-host driver-arm-64-android driver-arm-32-android

%/filters.h:
	mkdir -p $*
	make -C ../../ bin/test_simd_op_check
	cd $* && HL_TARGET=$* LD_LIBRARY_PATH=../../../bin ../../../bin/test_simd_op_check
	cat $*/test_*.h > $*/filter_headers.h
	echo "filter filters[] = {" > $*/filters.h
	cd $*; for f in test_*.h; do n=$${f/.h/}; echo '{"'$${n}'", &'$${n}'},'; done >> filters.h
	echo '{NULL, NULL}};' >> $*/filters.h

#driver-host driver-arm-32-android driver-arm-64-android : driver-%: driver.cpp %/filters.h
driver-%: driver.cpp %/filters.h
	$(CXX-$*) $(CXXFLAGS-$*) -I $* driver.cpp $*/test_*.o $*/simd_op_check_runtime.o -o driver-$* -ldl

clean:
	rm -rf test_*.h check_*.s test_*.o filters.h filter_headers.h driver-* arm-32-android arm-64-android host

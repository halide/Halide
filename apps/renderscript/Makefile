include ../support/Makefile.inc

TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
.PHONY: all $(TOP)
all: run
HALIDE_LIB := $(TOP)/$(LIB_HALIDE)
$(HALIDE_LIB): $(TOP)
	$(MAKE) -C $(TOP)

test_%: test_%.cpp
	g++ -std=c++11 -I ../../include/ $< -L ../../bin/ -lHalide -o $@ -g

generated_blur_rs_float.o generated_blur_rs_float.h generated_copy_rs_float.o generated_copy_rs_float.h \
generated_blur_rs_uint8.o generated_blur_rs_uint8.h generated_copy_rs_uint8.o generated_copy_rs_uint8.h: test_blur_copy $(HALIDE_LIB)
	LD_LIBRARY_PATH=../../bin DYLD_LIBRARY_PATH=../../bin HL_TARGET=arm-32-android-armv7s-renderscript ./$< "_rs"

generated_blur_arm_float.o generated_blur_arm_float.h generated_copy_arm_float.o generated_copy_arm_float.h \
generated_blur_arm_uint8.o generated_blur_arm_uint8.h generated_copy_arm_uint8.o generated_copy_arm_uint8.h: test_blur_copy $(HALIDE_LIB)
	LD_LIBRARY_PATH=../../bin DYLD_LIBRARY_PATH=../../bin HL_TARGET=arm-32-android-armv7s ./$< "_arm"

generated_test_two_kernels_rs.o generated_test_two_kernels_rs.h: test_two_kernels $(HALIDE_LIB)
	LD_LIBRARY_PATH=../../bin DYLD_LIBRARY_PATH=../../bin HL_TARGET=arm-32-android-armv7s-renderscript-debug ./$< "_rs"

generated_test_two_kernels_arm.o generated_test_two_kernels_arm.h: test_two_kernels $(HALIDE_LIB)
	LD_LIBRARY_PATH=../../bin DYLD_LIBRARY_PATH=../../bin HL_TARGET=arm-32-android-armv7s ./$< "_arm"

libs/armeabi-v7a/rstest: $(HALIDE_LIB) \
                         jni/rstest.cpp \
                         generated_copy_rs_float.o generated_copy_rs_float.h \
                         generated_copy_arm_float.o generated_copy_arm_float.h \
                         generated_blur_rs_float.o generated_blur_rs_float.h \
                         generated_blur_arm_float.o generated_blur_arm_float.h \
                         generated_copy_rs_uint8.o generated_copy_rs_uint8.h \
                         generated_copy_arm_uint8.o generated_copy_arm_uint8.h \
                         generated_blur_rs_uint8.o generated_blur_rs_uint8.h \
                         generated_blur_arm_uint8.o generated_blur_arm_uint8.h
	ndk-build libs/armeabi-v7a/librstest.so libs/armeabi-v7a/rstest-standalone libs/armeabi-v7a/libstlport_shared.so

libs/armeabi-v7a/rstest_two_kernels: $(HALIDE_LIB) \
                         jni/rstest_two_kernels.cpp \
                         generated_test_two_kernels_rs.o generated_test_two_kernels_rs.h \
                         generated_test_two_kernels_arm.o generated_test_two_kernels_arm.h
	ndk-build libs/armeabi-v7a/rstest_two_kernels-standalone libs/armeabi-v7a/libstlport_shared.so

deploy: libs/armeabi-v7a/rstest
	adb push libs/armeabi-v7a/librstest.so /mnt/sdcard/
	adb push libs/armeabi-v7a/rstest-standalone /mnt/sdcard/
	adb push libs/armeabi-v7a/libstlport_shared.so /mnt/sdcard/

define RUN_STEPS
su
mkdir /data/tmp
cd /data/tmp
pwd
cp /mnt/sdcard/rstest-standalone .
chmod 777 /data/tmp/rstest-standalone
cp /mnt/sdcard/libstlport_shared.so .
LD_LIBRARY_PATH=. ./rstest-standalone
exit
exit
endef
export RUN_STEPS

run: deploy
	adb logcat -c
	sh -c 'echo "$$RUN_STEPS" | adb shell'
	adb logcat -d | grep "^I/rstest"
	echo "Done"

deploy_two: libs/armeabi-v7a/rstest_two_kernels
	adb push libs/armeabi-v7a/rstest_two_kernels-standalone /mnt/sdcard/
	adb push libs/armeabi-v7a/libstlport_shared.so /mnt/sdcard/


define RUN_TWO_STEPS
su
mkdir /data/tmp
cd /data/tmp
pwd
cp /mnt/sdcard/rstest_two_kernels-standalone .
chmod 777 /data/tmp/rstest_two_kernels-standalone
cp /mnt/sdcard/libstlport_shared.so .
LD_LIBRARY_PATH=. ./rstest_two_kernels-standalone
exit
exit
endef
export RUN_TWO_STEPS


run-two: deploy_two
	adb logcat -c
	sh -c 'echo "$$RUN_TWO_STEPS" | adb shell'
	adb logcat -d | grep "halide"
	echo "Done"

clean:
	rm -f test_blur_copy
	rm -rf test_blur_copy.dSYM/
	rm -f test_two_kernels
	rm -rf test_two_kernels.dSYM/
	rm -f generated_*
	rm -rf libs/
	rm -rf obj/
	rm -rf bin/
	rm -rf gen/
	rm -rf proguard-project.txt
	rm -rf project.properties


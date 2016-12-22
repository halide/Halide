include ../support/Makefile.inc

TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
.PHONY: all $(TOP)
all: run run-two
HALIDE_LIB := $(TOP)/$(LIB_HALIDE)
$(HALIDE_LIB): $(TOP)
	$(MAKE) -C $(TOP)

test_%: test_%.cpp
	g++ -std=c++11 -I ../../include/ $< -L ../../bin/ -lHalide -o $@ -g

avg_filter_uint32t.o avg_filter_uint32t.h avg_filter_float.o avg_filter_float.h: test_oglc_avg
	LD_LIBRARY_PATH=../../bin DYLD_LIBRARY_PATH=../../bin HL_TARGET=arm-32-android-armv7s-openglcompute ./$<

avg_filter_uint32t_arm.o avg_filter_uint32t_arm.h avg_filter_float_arm.o avg_filter_float_arm.h: test_oglc_avg
	LD_LIBRARY_PATH=../../bin DYLD_LIBRARY_PATH=../../bin HL_TARGET=arm-32-android-armv7s ./$< "_arm"

AVG_FILTER_SRC = jni/oglc_run.cpp \
                 avg_filter_uint32t.o avg_filter_uint32t.h \
                 avg_filter_uint32t_arm.o avg_filter_uint32t_arm.h \
                 avg_filter_float.o avg_filter_float.h \
                 avg_filter_float_arm.o avg_filter_float_arm.h

libs/armeabi-v7a/oglc_run: $(HALIDE_LIB) $(AVG_FILTER_SRC)
	ndk-build libs/armeabi-v7a/oglc_run

two_kernels_filter.o two_kernels_filter.h: test_two_kernels
	LD_LIBRARY_PATH=../../bin DYLD_LIBRARY_PATH=../../bin HL_TARGET=arm-32-android-armv7s-openglcompute ./$<

TWO_KERNELS_SRC = jni/oglc_two_kernels_run.cpp \
                  two_kernels_filter.o two_kernels_filter.h

libs/armeabi-v7a/oglc_two_kernels_run: $(HALIDE_LIB) $(TWO_KERNELS_SRC)
	ndk-build libs/armeabi-v7a/oglc_two_kernels_run libs/armeabi-v7a/liboglc_two_kernels.so

jni-libs: $(HALIDE_LIB) $(AVG_FILTER_SRC) $(TWO_KERNELS_SRC)
	ndk-build libs/armeabi-v7a/liboglc_two_kernels.so libs/armeabi-v7a/liboglc.so

deploy: libs/armeabi-v7a/oglc_run
	adb push libs/armeabi-v7a/oglc_run /mnt/sdcard/

define RUN_STEPS
su
mkdir -p /data/tmp
rm -rf /data/tmp/oglc
mkdir /data/tmp/oglc
cd /data/tmp/oglc
pwd
cp /mnt/sdcard/oglc_run .
chmod 777 /data/tmp/oglc/oglc_run
LD_LIBRARY_PATH=. ./oglc_run
exit
exit
endef
export RUN_STEPS


run: deploy
	adb logcat -c
	sh -c 'echo "$$RUN_STEPS" | adb shell'
	adb logcat -d | grep "I oglc"
	echo "Done"

deploy-two: libs/armeabi-v7a/oglc_two_kernels_run
	adb push libs/armeabi-v7a/oglc_two_kernels_run /mnt/sdcard/


define RUN_TWO_STEPS
su
mkdir /data/tmp
cd /data/tmp
pwd
cp /mnt/sdcard/oglc_two_kernels_run .
chmod 777 /data/tmp/oglc_two_kernels_run
LD_LIBRARY_PATH=. ./oglc_two_kernels_run
exit
exit
endef
export RUN_TWO_STEPS

run-two: deploy-two
	adb logcat -c
	sh -c 'echo "$$RUN_TWO_STEPS" | adb shell'
	adb logcat -d | grep "I oglc"
	echo "Done"

clean:
	rm -f test_oglc_avg
	rm -rf test_oglc_avg.dSYM/
	rm -f avg_filter*
	rm -f test_two_kernels
	rm -rf test_two_kernels.dSYM/
	rm -rf libs/
	rm -rf obj/
	rm -rf bin/
	rm -rf gen/

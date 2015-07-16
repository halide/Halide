include ../support/Makefile.inc

TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
.PHONY: all $(TOP)
all: run
HALIDE_LIB := $(TOP)/$(LIB_HALIDE)
$(HALIDE_LIB): $(TOP)
	$(MAKE) -C $(TOP)

test_%: test_%.cpp
	g++ -std=c++11 -I ../../include/ $< -L ../../bin/ -lHalide -o $@ -g

avg_filter.o avg_filter.h: test_oglc_avg $(HALIDE_LIB)
	LD_LIBRARY_PATH=../../bin DYLD_LIBRARY_PATH=../../bin HL_TARGET=arm-32-android-armv7s-openglcompute-debug ./$<

libs/armeabi-v7a/oglc_run: $(HALIDE_LIB) \
                         jni/oglc_run.cpp \
                         avg_filter.o avg_filter.h
	ndk-build libs/armeabi-v7a/oglc_run libs/armeabi-v7a/libstlport_shared.so

deploy: libs/armeabi-v7a/oglc_run
	adb push libs/armeabi-v7a/oglc_run /mnt/sdcard/
	adb push libs/armeabi-v7a/libstlport_shared.so /mnt/sdcard/

define RUN_STEPS
su
mkdir /data/tmp
cd /data/tmp
pwd
cp /mnt/sdcard/oglc_run .
chmod 777 /data/tmp/oglc_run
cp /mnt/sdcard/libstlport_shared.so .
LD_LIBRARY_PATH=. ./oglc_run
exit
exit
endef
export RUN_STEPS

run: deploy
	adb logcat -c
	sh -c 'echo "$$RUN_STEPS" | adb shell'
	adb logcat -d | grep "^I/halide"
	echo "Done"

clean:
	rm -f test_oglc_avg
	rm -rf test_oglc_avg.dSYM/
	rm -f avg_filter.*
	rm -rf libs/
	rm -rf obj/


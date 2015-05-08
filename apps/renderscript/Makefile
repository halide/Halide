include ../support/Makefile.inc

TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
.PHONY: all $(TOP)
all: run
HALIDE_LIB := $(TOP)/$(LIB_HALIDE)
$(HALIDE_LIB): $(TOP)
	$(MAKE) -C $(TOP)

test_%: test_%.cpp
	g++ -std=c++11 -I ../../include/ $< -L ../../bin/ -lHalide -o $@ -g

generated_blur_rs.o generated_blur_rs.h generated_copy_rs.o generated_copy_rs.h: test_blur_copy $(HALIDE_LIB)
	LD_LIBRARY_PATH=../../bin DYLD_LIBRARY_PATH=../../bin HL_TARGET=arm-32-android-armv7s-renderscript ./$< "_rs"

generated_blur_arm.o generated_blur_arm.h generated_copy_arm.o generated_copy_arm.h: test_blur_copy $(HALIDE_LIB)
	LD_LIBRARY_PATH=../../bin DYLD_LIBRARY_PATH=../../bin HL_TARGET=arm-32-android-armv7s ./$< "_arm"

libs/armeabi-v7a/rstest: $(HALIDE_LIB) \
                         jni/rstest.cpp \
                         generated_copy_rs.o generated_copy_rs.h \
                         generated_copy_arm.o generated_copy_arm.h \
                         generated_blur_rs.o generated_blur_rs.h \
                         generated_blur_arm.o generated_blur_arm.h
	ndk-build

deploy: libs/armeabi-v7a/rstest
	adb push libs/armeabi-v7a/rstest-standalone /mnt/sdcard/
	adb push libs/armeabi-v7a/libstlport_shared.so /mnt/sdcard/

define DEPLOY_STEPS
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
export DEPLOY_STEPS

run: deploy
	sh -c 'echo "$$DEPLOY_STEPS" | adb shell'
	adb logcat -d | grep "^I/rstest"
	echo "Done"

clean:
	rm -f test_blur_copy
	rm -rf test_blur_copy.dSYM/
	rm -f generated_*
	rm -rf libs/
	rm -rf obj/
	rm -rf bin/
	rm -rf gen/
	rm -rf proguard-project.txt
	rm -rf project.properties


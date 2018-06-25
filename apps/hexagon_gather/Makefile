FEATURES ?= hvx_128-hvx_v65-hvx_scatter-hvx_gather
DEVICE_SIM ?= -DHL_HEXAGON_DEVICE
DEFINES ?= -DIMG_SIZE=1024 $(DEVICE_SIM)
echo DEFINES=\"${DEFINES}\"
export HL_VTCM_SIZE = 65536
RUNDROID = 1

SRC ?= $(shell pwd)
BIN ?= $(SRC)/bin

CXX-host ?= $(CXX)
ANDROID_NDK_ROOT ?= $(HEXAGON_SDK_ROOT)/tools/android-ndk-r14b
ANDROID_ARM64_TOOLCHAIN ?= $(ANDROID_NDK_ROOT)/install/android-21/arch-arm64
CXX-arm-64-android ?= $(ANDROID_ARM64_TOOLCHAIN)/bin/aarch64-linux-android-c++

HALIDE_ROOT ?= $(SRC)/../..
LIBHALIDE = $(HALIDE_ROOT)/lib/libHalide.a
GENGEN = $(HALIDE_ROOT)/tools/GenGen.cpp

CXXFLAGS = -std=c++11 -I$(HALIDE_ROOT)/include -I$(HALIDE_ROOT)/support -I$(SRC)/include
CXXFLAGS-host ?=
CXXFLAGS-arm-64-android ?=
GENERATOR_LDFLAGS =  -lpthread -ldl -g -lz
LDFLAGS-host ?= -lpthread -ldl -lm
LDFLAGS-arm-64-android ?= -llog -fPIE -pie

DEVICE_ITERATIONS ?= 10
SIMULATOR_ITERATIONS ?= 1
DASH_D_DEFINES=-DHALIDE_NO_PNG -DHALIDE_NO_JPEG

DEVICE_PATH ?= /data/hexagon_gather

.PHONY : run all clean

run : all
all: run-arm-64-android #run-host

$(BIN)/pipeline: pipeline.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $^ ${DEFINES} $(INCLUDES) $(LIBHALIDE) $(GENGEN) -o $@ $(GENERATOR_LDFLAGS)

$(BIN)/%/pipeline.o: $(BIN)/pipeline
	@mkdir -p $(@D)
	$^ -g gather -o $(BIN)/$* -e o,h -f pipeline target=$*-$(FEATURES)

$(BIN)/process-%: process.cpp $(BIN)/%/pipeline.o
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$* -Wall -O3 process.cpp ${DEFINES} $(BIN)/$*/pipeline.o -o $(BIN)/process-$* $(LDFLAGS-$*) $(DASH_D_DEFINES)

run-%-android: $(BIN)/process-%-android
		adb shell mkdir -p $(DEVICE_PATH); \
		adb push $(BIN)/process-$*-android $(DEVICE_PATH); \
		adb shell chmod +x $(DEVICE_PATH)/process-$*-android; \
		adb shell FEATURES=$(FEATURES) $(DEVICE_PATH)/process-$*-android $(DEVICE_ITERATIONS) 2>&1 | tee $(BIN)/run.stdout ; \

run-host: $(BIN)/process-host
	FEATURES=$(FEATURES) $(BIN)/process-host $(SIMULATOR_ITERATIONS) 2>&1 | tee $(BIN)/run.stdout

clean:
	rm -rf $(BIN)

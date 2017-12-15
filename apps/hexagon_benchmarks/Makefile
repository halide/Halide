include ../support/Makefile.inc


# This app requires a separate toolchain to be built from the Android NDK,
# using the make-standalone-toolchain.sh script:
#$ build/tools/make-standalone-toolchain.sh --arch=arm64 --platform=android-21 --install-dir=$ANDROID_ARM64_TOOLCHAIN
#$ build/tools/make-standalone-toolchain.sh --arch=arm --platform=android-21 --install-dir=$ANDROID_ARM_TOOLCHAIN
CXX-host ?= $(CXX)
CXX-arm-64-android ?= $(ANDROID_ARM64_TOOLCHAIN)/bin/aarch64-linux-android-c++
CXX-arm-32-android ?= $(ANDROID_ARM_TOOLCHAIN)/bin/arm-linux-androideabi-c++
CXX-arm-64-profile-android ?= $(CXX-arm-64-android)
CXX-arm-32-profile-android ?= $(CXX-arm-32-android)

CXXFLAGS-host ?=
CXXFLAGS-arm-64-android ?=
CXXFLAGS-arm-32-android ?=

LDFLAGS-host ?= -lpthread -ldl -lm
LDFLAGS-arm-64-android ?= -llog -fPIE -pie
LDFLAGS-arm-32-android ?= -llog -fPIE -pie
LDFLAGS-arm-64-profile-android ?= -llog -fPIE -pie
LDFLAGS-arm-32-profile-android ?= -llog -fPIE -pie

BIN ?= bin

FILTERS ?= conv3x3a16 dilate3x3 median3x3 gaussian5x5 sobel conv3x3a32

ITERATIONS ?= 10

OBJS = $(patsubst %,$(BIN)/\%/%_hvx64.o, $(FILTERS))
OBJS += $(patsubst %,$(BIN)/\%/%_hvx128.o, $(FILTERS))
OBJS += $(patsubst %,$(BIN)/\%/%_cpu.o, $(FILTERS))

LINK_OBJS = $(patsubst %,$(BIN)/$$*/%_hvx64.o, $(FILTERS))
LINK_OBJS += $(patsubst %,$(BIN)/$$*/%_hvx128.o, $(FILTERS))
LINK_OBJS += $(patsubst %,$(BIN)/$$*/%_cpu.o, $(FILTERS))

UPPERCASE_FILTERS = $(shell echo $(FILTERS) | tr '[:lower:]' '[:upper:]')
DASH_D_DEFINES = $(patsubst %, -D%=1, $(UPPERCASE_FILTERS))

PARALLEL_SCHED ?= true
PREFETCH_SCHED ?= true
SCHEDULING_OPTS = use_parallel_sched=${PARALLEL_SCHED} use_prefetch_sched=${PREFETCH_SCHED}

$(BIN)/%.generator : %_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -O3 -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/%/conv3x3a16_cpu.o: $(BIN)/conv3x3.generator
	@mkdir -p $(@D)
	$^ -g conv3x3 -o $(BIN)/$* -e o,h -f conv3x3a16_cpu target=$* accumulator_type=int16 ${SCHEDULING_OPTS}

$(BIN)/%/conv3x3a16_hvx64.o: $(BIN)/conv3x3.generator
	@mkdir -p $(@D)
	$^ -g conv3x3 -o $(BIN)/$* -e o,h -f conv3x3a16_hvx64 target=$*-hvx_64 accumulator_type=int16 ${SCHEDULING_OPTS}

$(BIN)/%/conv3x3a16_hvx128.o: $(BIN)/conv3x3.generator
	@mkdir -p $(@D)
	$^ -g conv3x3 -o $(BIN)/$* -e o,h -f conv3x3a16_hvx128 target=$*-hvx_128 accumulator_type=int16 ${SCHEDULING_OPTS}

$(BIN)/%/dilate3x3_cpu.o: $(BIN)/dilate3x3.generator
	@mkdir -p $(@D)
	$^ -g dilate3x3 -o $(BIN)/$* -e o,h -f dilate3x3_cpu target=$* ${SCHEDULING_OPTS}

$(BIN)/%/dilate3x3_hvx64.o: $(BIN)/dilate3x3.generator
	@mkdir -p $(@D)
	$^ -g dilate3x3 -o $(BIN)/$* -e o,h -f dilate3x3_hvx64 target=$*-hvx_64 ${SCHEDULING_OPTS}

$(BIN)/%/dilate3x3_hvx128.o: $(BIN)/dilate3x3.generator
	@mkdir -p $(@D)
	$^ -g dilate3x3 -o $(BIN)/$* -e o,h -f dilate3x3_hvx128 target=$*-hvx_128 ${SCHEDULING_OPTS}

$(BIN)/%/median3x3_cpu.o: $(BIN)/median3x3.generator
	@mkdir -p $(@D)
	$^ -g median3x3 -o $(BIN)/$* -e o,h -f median3x3_cpu target=$* ${SCHEDULING_OPTS}

$(BIN)/%/median3x3_hvx64.o: $(BIN)/median3x3.generator
	@mkdir -p $(@D)
	$^ -g median3x3 -o $(BIN)/$* -e o,h -f median3x3_hvx64 target=$*-hvx_64 ${SCHEDULING_OPTS}

$(BIN)/%/median3x3_hvx128.o: $(BIN)/median3x3.generator
	@mkdir -p $(@D)
	$^ -g median3x3 -o $(BIN)/$* -e o,h -f median3x3_hvx128 target=$*-hvx_128 ${SCHEDULING_OPTS}

$(BIN)/%/gaussian5x5_cpu.o: $(BIN)/gaussian5x5.generator
	@mkdir -p $(@D)
	$^ -g gaussian5x5 -o $(BIN)/$* -e o,h -f gaussian5x5_cpu target=$* ${SCHEDULING_OPTS}

$(BIN)/%/gaussian5x5_hvx64.o: $(BIN)/gaussian5x5.generator
	@mkdir -p $(@D)
	$^ -g gaussian5x5 -o $(BIN)/$* -e o,h -f gaussian5x5_hvx64 target=$*-hvx_64 ${SCHEDULING_OPTS}

$(BIN)/%/gaussian5x5_hvx128.o: $(BIN)/gaussian5x5.generator
	@mkdir -p $(@D)
	$^ -g gaussian5x5 -o $(BIN)/$* -e o,h -f gaussian5x5_hvx128 target=$*-hvx_128 ${SCHEDULING_OPTS}

$(BIN)/%/sobel_cpu.o: $(BIN)/sobel.generator
	@mkdir -p $(@D)
	$^ -g sobel -o $(BIN)/$* -e o,h -f sobel_cpu target=$* ${SCHEDULING_OPTS}

$(BIN)/%/sobel_hvx64.o: $(BIN)/sobel.generator
	@mkdir -p $(@D)
	$^ -g sobel -o $(BIN)/$* -e o,h -f sobel_hvx64 target=$*-hvx_64 ${SCHEDULING_OPTS}

$(BIN)/%/sobel_hvx128.o: $(BIN)/sobel.generator
	@mkdir -p $(@D)
	$^ -g sobel -o $(BIN)/$* -e o,h -f sobel_hvx128 target=$*-hvx_128 ${SCHEDULING_OPTS}

$(BIN)/%/conv3x3a32_cpu.o: $(BIN)/conv3x3.generator
	@mkdir -p $(@D)
	$^ -g conv3x3 -o $(BIN)/$* -e o,h -f conv3x3a32_cpu target=$* accumulator_type=int32 ${SCHEDULING_OPTS}

$(BIN)/%/conv3x3a32_hvx64.o: $(BIN)/conv3x3.generator
	@mkdir -p $(@D)
	$^ -g conv3x3 -o $(BIN)/$* -e o,h -f conv3x3a32_hvx64 target=$*-hvx_64 accumulator_type=int32 ${SCHEDULING_OPTS}

$(BIN)/%/conv3x3a32_hvx128.o: $(BIN)/conv3x3.generator
	@mkdir -p $(@D)
	$^ -g conv3x3 -o $(BIN)/$* -e o,h -f conv3x3a32_hvx128 target=$*-hvx_128 accumulator_type=int32 ${SCHEDULING_OPTS}

$(BIN)/%/filters.a : $(OBJS)
	ar q $(BIN)/$*/filters.a $^

$(BIN)/%/process: process.cpp $(BIN)/%/filters.a process.h
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$*  $(DASH_D_DEFINES) -Wall -O0 -g process.cpp $(BIN)/$*/filters.a -o $(BIN)/$*/process $(LDFLAGS-$*)

DEVICE_PATH ?= /data/local/tmp/hexagon_benchmarks
DEVICE_ENV = "LD_LIBRARY_PATH=$(DEVICE_PATH):/vendor/lib64 ADSP_LIBRARY_PATH=\"$(DEVICE_PATH);/dsp\""
HEXAGON_RUNTIME_PATH = $(HALIDE_SRC_PATH)/src/runtime/hexagon_remote
run-%-android: $(BIN)/%-android/process
	adb shell mkdir -p $(DEVICE_PATH)
	adb push $(BIN)/$*-android/process $(DEVICE_PATH)
	adb push $(HEXAGON_RUNTIME_PATH)/bin/$*-android/libhalide_hexagon_host.so $(DEVICE_PATH)
	adb push $(HEXAGON_RUNTIME_PATH)/bin/v60/signed_by_debug/libhalide_hexagon_remote_skel.so $(DEVICE_PATH)
	adb shell cp /system/lib/rfsa/adsp/testsig* $(DEVICE_PATH) > /dev/null || true
	adb shell chmod +x $(DEVICE_PATH)/process
	adb shell $(DEVICE_ENV) $(DEVICE_PATH)/process -n $(ITERATIONS)

run-host: $(BIN)/host/process
	$(BIN)/host/process -n $(ITERATIONS)

clean:
	rm -rf $(BIN)

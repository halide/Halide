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

OBJS = $(patsubst %,$(BIN)/\%/%_hvx64.o, $(FILTERS))
OBJS += $(patsubst %,$(BIN)/\%/%_hvx128.o, $(FILTERS))
OBJS += $(patsubst %,$(BIN)/\%/%_cpu.o, $(FILTERS))

LINK_OBJS = $(patsubst %,$(BIN)/$$*/%_hvx64.o, $(FILTERS))
LINK_OBJS += $(patsubst %,$(BIN)/$$*/%_hvx128.o, $(FILTERS))
LINK_OBJS += $(patsubst %,$(BIN)/$$*/%_cpu.o, $(FILTERS))

UPPERCASE_FILTERS = $(shell echo $(FILTERS) | tr '[:lower:]' '[:upper:]')
DASH_D_DEFINES = $(patsubst %, -D%=1, $(UPPERCASE_FILTERS))

$(BIN)/%_generator : %_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -O3 -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/%/conv3x3a16_cpu.o: $(BIN)/conv3x3_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f conv3x3a16_cpu target=$* accumulator_type=int16

$(BIN)/%/conv3x3a16_hvx64.o: $(BIN)/conv3x3_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f conv3x3a16_hvx64 target=$*-hvx_64 accumulator_type=int16

$(BIN)/%/conv3x3a16_hvx128.o: $(BIN)/conv3x3_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f conv3x3a16_hvx128 target=$*-hvx_128 accumulator_type=int16

$(BIN)/%/dilate3x3_cpu.o: $(BIN)/dilate3x3_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f dilate3x3_cpu target=$*

$(BIN)/%/dilate3x3_hvx64.o: $(BIN)/dilate3x3_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f dilate3x3_hvx64 target=$*-hvx_64

$(BIN)/%/dilate3x3_hvx128.o: $(BIN)/dilate3x3_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f dilate3x3_hvx128 target=$*-hvx_128

$(BIN)/%/median3x3_cpu.o: $(BIN)/median3x3_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f median3x3_cpu target=$*

$(BIN)/%/median3x3_hvx64.o: $(BIN)/median3x3_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f median3x3_hvx64 target=$*-hvx_64

$(BIN)/%/median3x3_hvx128.o: $(BIN)/median3x3_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f median3x3_hvx128 target=$*-hvx_128

$(BIN)/%/gaussian5x5_cpu.o: $(BIN)/gaussian5x5_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f gaussian5x5_cpu target=$*

$(BIN)/%/gaussian5x5_hvx64.o: $(BIN)/gaussian5x5_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f gaussian5x5_hvx64 target=$*-hvx_64

$(BIN)/%/gaussian5x5_hvx128.o: $(BIN)/gaussian5x5_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f gaussian5x5_hvx128 target=$*-hvx_128

$(BIN)/%/sobel_cpu.o: $(BIN)/sobel_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f sobel_cpu target=$*

$(BIN)/%/sobel_hvx64.o: $(BIN)/sobel_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f sobel_hvx64 target=$*-hvx_64

$(BIN)/%/sobel_hvx128.o: $(BIN)/sobel_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f sobel_hvx128 target=$*-hvx_128

$(BIN)/%/conv3x3a32_cpu.o: $(BIN)/conv3x3_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f conv3x3a32_cpu target=$* accumulator_type=int32

$(BIN)/%/conv3x3a32_hvx64.o: $(BIN)/conv3x3_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f conv3x3a32_hvx64 target=$*-hvx_64 accumulator_type=int32

$(BIN)/%/conv3x3a32_hvx128.o: $(BIN)/conv3x3_generator
	@-mkdir -p $(BIN)/$*
	$^ -o $(BIN)/$* -e o,h -f conv3x3a32_hvx128 target=$*-hvx_128 accumulator_type=int32

$(BIN)/%/filters.a : $(OBJS)
	ar q $(BIN)/$*/filters.a $^

$(BIN)/process-%: process.cpp $(BIN)/%/filters.a process.h
	$(CXX-$*) $(CXXFLAGS) $(CXXFLAGS-$*) -I $(BIN)/$*  $(DASH_D_DEFINES) -Wall -O0 -g process.cpp $(BIN)/$*/filters.a -o $(BIN)/process-$* $(LDFLAGS-$*)

run-%-android: $(BIN)/process-%-android
	adb push $(BIN)/process-$*-android /data/
	adb shell chmod +x /data/process-$*-android
	adb shell /data/process-$*-android -n 10

run-host: $(BIN)/process-host
	$(BIN)/process-host -n 10

clean:
	rm -rf $(BIN)

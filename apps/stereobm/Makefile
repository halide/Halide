include ../support/Makefile.inc

# process.cpp compares against OpenCV's StereoBM.
# Auto-disable if opencv4 calib3d header is not available.
OPENCV_CXXFLAGS := $(shell pkg-config --cflags opencv4 2>/dev/null)
OPENCV_LIBS := $(shell pkg-config --libs opencv4 2>/dev/null)
HAVE_OPENCV_CALIB3D := $(shell echo '\#include <opencv2/calib3d.hpp>' | $(CXX) $(OPENCV_CXXFLAGS) -x c++ - -fsyntax-only 2>/dev/null && echo yes)
STEREOBM_BUILD_OPENCV ?= $(if $(HAVE_OPENCV_CALIB3D),1,0)

# We set parameters here to feed into both OpenCV and our generator, so that they are consistent.
STEREOBM_WINSIZE ?= 9
STEREOBM_DEPTH ?= 16
STEREOBM_FILTERCAP ?= 31
STEREOBM_THRESHOLD ?= 10
STEREOBM_UNIQUENESS ?= 0
STEREOBM_MINDISP ?= 0
STEREOBM_TILESIZE ?= 64

STEREOBM_GEN_PARAMS := winsize=$(STEREOBM_WINSIZE) depth=$(STEREOBM_DEPTH) tilesize=$(STEREOBM_TILESIZE) threshold=$(STEREOBM_THRESHOLD) mindisp=$(STEREOBM_MINDISP) uniqueness_ratio=$(STEREOBM_UNIQUENESS) filtercap=$(STEREOBM_FILTERCAP)

STEREOBM_DEFINES := -DWINSIZE=$(STEREOBM_WINSIZE) -DDEPTH=$(STEREOBM_DEPTH) -DPREFILTER_CAP=$(STEREOBM_FILTERCAP) -DTEXTURE_THRESHOLD=$(STEREOBM_THRESHOLD) -DUNIQUENESS_RATIO=$(STEREOBM_UNIQUENESS) -DMIN_DISP=$(STEREOBM_MINDISP)

# Always build and test the no-OpenCV variant; add OpenCV if available
BUILD_TARGETS := $(BIN)/$(HL_TARGET)/process
TEST_OUTPUTS := $(BIN)/$(HL_TARGET)/out.png
ifeq ($(STEREOBM_BUILD_OPENCV),1)
  BUILD_TARGETS += $(BIN)/$(HL_TARGET)/process_opencv
  TEST_OUTPUTS += $(BIN)/$(HL_TARGET)/out2.png
endif

.PHONY: build clean test

build: $(BUILD_TARGETS)

$(GENERATOR_BIN)/stereobm.generator: stereobm_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/stereobm.a: $(GENERATOR_BIN)/stereobm.generator
	@mkdir -p $(@D)
	$^ -g stereobm -e $(GENERATOR_OUTPUTS) -o $(@D) -f stereobm target=$* $(STEREOBM_GEN_PARAMS)

$(BIN)/%/process_opencv: process.cpp $(BIN)/%/stereobm.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DSTEREOBM_BUILD_OPENCV=1 $(STEREOBM_DEFINES) $(OPENCV_CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENCV_LIBS)

$(BIN)/%/process: process.cpp $(BIN)/%/stereobm.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DSTEREOBM_BUILD_OPENCV=0 $(STEREOBM_DEFINES) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process_opencv
	@mkdir -p $(@D)
	$< $(IMAGES)/aloeL.png $(IMAGES)/aloeR.png $@

$(BIN)/%/out2.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/aloeL.png $(IMAGES)/aloeR.png $@

clean:
	rm -rf $(BIN)

test: $(TEST_OUTPUTS)

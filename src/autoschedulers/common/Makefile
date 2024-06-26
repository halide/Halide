THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
COMMON_DIR = $(strip $(shell dirname $(THIS_MAKEFILE)))
HALIDE_SRC_ROOT = $(realpath $(COMMON_DIR)/../../../)

HALIDE_DISTRIB_PATH ?= $(HALIDE_SRC_ROOT)/distrib

# Don't include an autoscheduler in the generator deps
AUTOSCHEDULER=
include $(HALIDE_SRC_ROOT)/apps/support/Makefile.inc


$(BIN)/featurization_to_sample: $(COMMON_DIR)/featurization_to_sample.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OPTIMIZE) -o $@

$(BIN)/get_host_target: $(COMMON_DIR)/get_host_target.cpp $(LIB_HALIDE) $(HALIDE_DISTRIB_PATH)/include/Halide.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) $(LIBHALIDE_LDFLAGS) $(OPTIMIZE) -o $@



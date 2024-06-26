THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
SRC  = $(strip $(shell dirname $(THIS_MAKEFILE)))
HALIDE_ROOT = $(realpath $(SRC)/../../../)
COMMON_DIR =  $(realpath $(SRC)/../common/)

HALIDE_DISTRIB_PATH ?= $(HALIDE_SRC_ROOT)/distrib
include $(HALIDE_ROOT)/apps/support/Makefile.inc

# Add the relative location of libHalide.so in the rpath in a distro so that the autoscheduler library can find libHalide
ifeq ($(UNAME), Darwin)
HALIDE_RPATH_FOR_LIB += '-Wl,-rpath,@loader_path'
else
HALIDE_RPATH_FOR_LIB += '-Wl,-rpath,$$ORIGIN'
endif

CXXFLAGS += -I$(COMMON_DIR)

# Be sure *not* to include libHalide in the link steps here; that can cause misbehavior
# on OSX systems in certain situations -- note that $(LIB_HALIDE) is an order-only dep,
# to ensure that (eg) Halide.h is built before this.
$(BIN)/libautoschedule_mullapudi2016.$(PLUGIN_EXT): $(SRC)/AutoSchedule.cpp | $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC -fvisibility=hidden -fvisibility-inlines-hidden $(CXXFLAGS) $(OPTIMIZE) $^ -o $@ $(HALIDE_RPATH_FOR_LIB)

THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
SRC  = $(strip $(shell dirname $(THIS_MAKEFILE)))
HALIDE_SRC_ROOT = $(realpath $(SRC)/../../../)
COMMON_DIR =  $(realpath $(SRC)/../common/)

# Assume an in-tree build of a halide distro exists. Most uses of this
# Makefile should probably set this variable explicitly.
HALIDE_DISTRIB_PATH ?= $(HALIDE_SRC_ROOT)/distrib

include $(HALIDE_SRC_ROOT)/apps/support/Makefile.inc

CXXFLAGS += -I$(COMMON_DIR)

ifeq ($(UNAME), Darwin)
HALIDE_RPATH_FOR_LIB += '-Wl,-rpath,@loader_path'
else
HALIDE_RPATH_FOR_LIB += '-Wl,-rpath,$$ORIGIN'
endif

# Be sure *not* to include libHalide in the link steps here; that can cause misbehavior
# on OSX systems in certain situations -- note that $(LIB_HALIDE) is an order-only dep,
# to ensure that (eg) Halide.h is built before this.
$(BIN)/libautoschedule_li2018.$(PLUGIN_EXT): $(SRC)/GradientAutoscheduler.cpp | $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC -fvisibility=hidden -fvisibility-inlines-hidden $(CXXFLAGS) $(OPTIMIZE) $^ -o $@ $(HALIDE_SYSTEM_LIBS) $(HALIDE_RPATH_FOR_LIB)


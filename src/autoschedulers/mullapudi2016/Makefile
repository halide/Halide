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

# It's important to use dynamic lookups for undefined symbols here: all of libHalide
# is expected to be present (in the loading binary), so we explicitly make the symbols
# undefined rather than dependent on libHalide.so.
$(BIN)/libautoschedule_mullapudi2016.$(SHARED_EXT): $(SRC)/AutoSchedule.cpp
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC -fvisibility=hidden -fvisibility-inlines-hidden $(CXXFLAGS) $(OPTIMIZE) $^ -o $@ $(HALIDE_RPATH_FOR_LIB)

THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
SRC  = $(strip $(shell dirname $(THIS_MAKEFILE)))
HALIDE_ROOT = $(realpath $(SRC)/../../../)
COMMON_DIR =  $(realpath $(SRC)/../common/)

HALIDE_DISTRIB_PATH ?= $(HALIDE_SRC_ROOT)/distrib
include $(HALIDE_ROOT)/apps/support/Makefile.inc

CXXFLAGS += -I$(COMMON_DIR)

$(BIN)/libautoschedule_mullapudi2016.so: $(SRC)/AutoSchedule.cpp $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC -fvisibility=hidden -fvisibility-inlines-hidden $(CXXFLAGS) $(OPTIMIZE) $^ -o $@ $(HALIDE_SYSTEM_LIBS)

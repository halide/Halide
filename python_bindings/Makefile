UNAME = $(shell uname)
THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
ROOT_DIR = $(strip $(shell dirname $(THIS_MAKEFILE)))

# These are set by Halide's Makefile when built via that path.
HALIDE_DISTRIB_PATH ?= $(ROOT_DIR)/../distrib
BIN ?= $(ROOT_DIR)/bin
# 'python' or 'python3'
PYTHON ?= python3
TEST_TMP ?= $(BIN)/tmp

ifeq ($(OS), Windows_NT)
    # assume we are building for the MinGW environment
    FPIC=
    SHARED_EXT=dll
else
    FPIC=-fPIC
ifeq ($(UNAME), Darwin)
    SHARED_EXT=dylib
else
    SHARED_EXT=so
endif
endif

LIBHALIDE ?= $(HALIDE_DISTRIB_PATH)/bin/libHalide.$(SHARED_EXT)

ifndef PYBIND11_PATH
    $(error PYBIND11_PATH is undefined)
endif
PYBIND11_PATH ?= /path/to/pybind11

OPTIMIZE ?= -O3

# defining DEBUG + undefining NDEBUG gives extra debug info in PyBind11
# OPTIMIZE ?= -g -DDEBUG=1 -UNDEBUG

# Compiling with -fvisibility=hidden saves ~80k on optimized x64 builds
CCFLAGS=$(shell $(PYTHON)-config --cflags) -I $(PYBIND11_PATH)/include -I $(HALIDE_DISTRIB_PATH)/include -I $(ROOT_DIR) -std=c++11 $(FPIC) -fvisibility=hidden -fvisibility-inlines-hidden $(OPTIMIZE)
LDFLAGS=$(shell $(PYTHON)-config --ldflags) -lz

ifeq ($(UNAME), Darwin)
	# Some Mac Python3 installs incorrectly include "-lintl" in --ldflags, 
	# which isn't needed and may be missing. Just strip it.
	LDFLAGS := $(filter-out -lintl,$(LDFLAGS))
endif

# Filter out a pointless warning present in some Python installs
CCFLAGS := $(filter-out -Wstrict-prototypes,$(CCFLAGS))


PY_SRCS=$(shell ls $(ROOT_DIR)/src/*.cpp)
PY_OBJS=$(PY_SRCS:$(ROOT_DIR)/src/%.cpp=$(BIN)/src/%.o)

# Note that Python on OSX won't load .dylib; it requires .so
$(BIN)/halide.so: $(PY_OBJS) $(LIBHALIDE)
	@mkdir -p $(@D)
	$(CXX) $^ $(LDFLAGS) -shared -o $@

$(BIN)/src/%.o: $(ROOT_DIR)/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CCFLAGS) -c $< -o $@


$(BIN)/%_generator.o: $(ROOT_DIR)/correctness/%_generator.cpp $(HALIDE_DISTRIB_PATH)/include/Halide.h
	@mkdir -p $(@D)
	$(CXX) $(CCFLAGS) -c $< -o $@

$(BIN)/PyStubImpl.o: $(ROOT_DIR)/stub/PyStubImpl.cpp $(HALIDE_DISTRIB_PATH)/include/Halide.h
	@mkdir -p $(@D)
	$(CXX) $(CCFLAGS) -c $< -o $@

# Produce a Python extension for the generator by compiling PyStub.cpp
# (with HALIDE_PYSTUB_GENERATOR_NAME defined to the Generator's build name), 
# and linking with the generator's .o file, PyStubImpl.o, plus the same libHalide
# being used by halide.so. 
#
# You can optionally also define HALIDE_PYSTUB_MODULE_NAME if you want the Python 
# module name to be something other than the Generator build name.
$(BIN)/%_PyStub.o: $(ROOT_DIR)/stub/PyStub.cpp
	@mkdir -p $(@D)
	$(CXX) $(CCFLAGS) -DHALIDE_PYSTUB_GENERATOR_NAME=$* -c $< -o $@

$(BIN)/%.so: $(BIN)/%_PyStub.o $(BIN)/PyStubImpl.o $(BIN)/%_generator.o $(LIBHALIDE)
	@mkdir -p $(@D)
	$(CXX) $^ $(LDFLAGS) -shared -o $@

test_correctness_pystub: $(BIN)/simplestub.so $(BIN)/complexstub.so 

APPS = $(shell ls $(ROOT_DIR)/apps/*.py)
CORRECTNESS = $(shell ls $(ROOT_DIR)/correctness/*.py)
TUTORIAL = $(shell ls $(ROOT_DIR)/tutorial/*.py)

.PHONY: test_apps
test_apps: $(APPS:$(ROOT_DIR)/apps/%.py=test_apps_%)

test_apps_%: $(ROOT_DIR)/apps/%.py $(BIN)/halide.so
	@echo Testing $*...
	@mkdir -p $(TEST_TMP)
	@# Send stdout (but not stderr) from these to /dev/null to reduce noise
	@cd $(TEST_TMP); PYTHONPATH="$(BIN):$$PYTHONPATH" $(PYTHON) $< >/dev/null

.PHONY: test_correctness
test_correctness: $(CORRECTNESS:$(ROOT_DIR)/correctness/%.py=test_correctness_%)

test_correctness_%: $(ROOT_DIR)/correctness/%.py $(BIN)/halide.so
	@echo Testing $*...
	@mkdir -p $(TEST_TMP)
	@cd $(TEST_TMP); PYTHONPATH="$(BIN):$$PYTHONPATH" $(PYTHON) $<

.PHONY: test_tutorial
test_tutorial: $(TUTORIAL:$(ROOT_DIR)/tutorial/%.py=test_tutorial_%)

test_tutorial_%: $(ROOT_DIR)/tutorial/%.py $(BIN)/halide.so
	@echo Testing $*...
	@mkdir -p $(TEST_TMP)
	@# Send stdout (but not stderr) from these to /dev/null to reduce noise
	@cd $(TEST_TMP); PYTHONPATH="$(BIN):$$PYTHONPATH" $(PYTHON) $< >/dev/null

clean:
	rm -rf $(BIN)

test: test_correctness test_apps test_tutorial

UNAME = $(shell uname)
THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
ROOT_DIR = $(strip $(shell dirname $(THIS_MAKEFILE)))

# These are set by Halide's Makefile when built via that path.
HALIDE_DISTRIB_PATH ?= $(ROOT_DIR)/../distrib
BIN ?= $(ROOT_DIR)/bin
# 'python' or 'python3'
PYTHON ?= python3
TEST_TMP ?= $(BIN)/tmp

ifndef PYBIND11_PATH
    $(error PYBIND11_PATH is undefined)
endif
PYBIND11_PATH ?= /path/to/pybind11

OPTIMIZE ?= -O3

# defining DEBUG + undefining NDEBUG gives extra debug info in PyBind11
# OPTIMIZE ?= -g -DDEBUG=1 -UNDEBUG

# Compiling with -fvisibility=hidden saves ~80k on optimized x64 builds
CCFLAGS=$(shell $(PYTHON)-config --cflags) -I $(PYBIND11_PATH)/include -I $(HALIDE_DISTRIB_PATH)/include -I $(ROOT_DIR) -std=c++11 -fPIC -fvisibility=hidden $(OPTIMIZE)
LDFLAGS=$(shell $(PYTHON)-config --ldflags) -lz

PY_SRCS=$(shell ls $(ROOT_DIR)/src/*.cpp)
PY_OBJS=$(PY_SRCS:$(ROOT_DIR)/src/%.cpp=$(BIN)/py_%.o)

$(BIN)/halide.so: $(PY_SRCS) $(PY_OBJS) $(HALIDE_DISTRIB_PATH)/lib/libHalide.a
	@mkdir -p $(@D)
	$(CXX) $(PY_OBJS) $(LDFLAGS) $(HALIDE_DISTRIB_PATH)/lib/libHalide.a -shared -o $@

$(BIN)/py_%.o: $(ROOT_DIR)/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CCFLAGS) -c $< -o $@

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

UNAME = $(shell uname)
THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
ROOT_DIR = $(strip $(shell dirname $(THIS_MAKEFILE)))

# These are set by Halide's Makefile when built via that path.
HALIDE_DISTRIB_PATH ?= $(ROOT_DIR)/../distrib
BIN ?= $(ROOT_DIR)/bin
# 'python' or 'python3'
PYTHON ?= python3
TEST_TMP ?= $(BIN)/tmp

ifeq ($(UNAME), Linux)
	# Disable some warnings that are pervasive in Boost
	CCFLAGS=$(shell $(PYTHON)-config --cflags) -I $(HALIDE_DISTRIB_PATH)/include -I $(ROOT_DIR) -std=c++11 -fPIC -Wno-unused-local-typedef -Wno-shorten-64-to-32
	PYTHON_VER=$(shell $(PYTHON) --version 2>&1 | cut -d' ' -f2 | cut -b1,3)
	LDFLAGS=$(shell $(PYTHON)-config --ldflags) -lboost_python-py$(PYTHON_VER) -lz
else ifeq ($(UNAME), Darwin)
	# The /opt includes are in case this is a macports install of python/boost.python
	ifneq ("$(wildcard /opt/local)","")
		MACPORTS_CCFLAGS = -I /opt/local/include
		MACPORTS_LDFLAGS = -L /opt/local/lib
	else
		MACPORTS_CCFLAGS = 
		MACPORTS_LDFLAGS = 
	endif
	PYTHON_VER=$(shell $(PYTHON) --version 2>&1 | cut -d' ' -f2 | cut -b1,1)
	ifeq ($(PYTHON_VER), 3)
	   BOOST_PYTHON_LIB = boost_python3-mt
	else
	   BOOST_PYTHON_LIB = boost_python-mt
	endif
	CCFLAGS=$(shell $(PYTHON)-config --cflags) -I $(HALIDE_DISTRIB_PATH)/include -I $(ROOT_DIR) $(MACPORTS_CCFLAGS) -std=c++11 -Wno-unused-local-typedef -Wno-shorten-64-to-32
	LDFLAGS=$(shell $(PYTHON)-config --ldflags) $(MACPORTS_LDFLAGS) -l$(BOOST_PYTHON_LIB) -lz
else
	CCFLAGS = $(error Unsupported platform $(UNAME))
endif

NUMPY_PATH=$(shell $(PYTHON) -c "import numpy; print(numpy.__path__[0] + '/core/include')")

PY_SRCS=$(shell ls $(ROOT_DIR)/src/*.cpp)
PY_OBJS=$(PY_SRCS:$(ROOT_DIR)/src/%.cpp=$(BIN)/py_%.o)
NUMPY_SRCS=$(shell ls $(ROOT_DIR)/halide_numpy/*.cpp)
NUMPY_OBJS=$(NUMPY_SRCS:$(ROOT_DIR)/halide_numpy/%.cpp=$(BIN)/numpy_%.o)

$(BIN)/halide.so: $(PY_SRCS) $(PY_OBJS) $(NUMPY_SRCS) $(NUMPY_OBJS)  $(HALIDE_DISTRIB_PATH)/lib/libHalide.a
	@mkdir -p $(@D)
	$(CXX) $(PY_OBJS) $(NUMPY_OBJS) $(LDFLAGS) $(HALIDE_DISTRIB_PATH)/lib/libHalide.a -shared -o $@

$(BIN)/py_%.o: $(ROOT_DIR)/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CCFLAGS) -c $< -o $@

$(BIN)/numpy_%.o: $(ROOT_DIR)/halide_numpy/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CCFLAGS) -I $(NUMPY_PATH) -c $< -o $@

APPS = $(shell ls $(ROOT_DIR)/apps/*.py)
CORRECTNESS = $(shell ls $(ROOT_DIR)/correctness/*.py)
TUTORIAL = $(shell ls $(ROOT_DIR)/tutorial/*.py)

.PHONY: test_apps
test_apps: $(APPS:$(ROOT_DIR)/apps/%.py=test_apps_%)

test_apps_%: $(ROOT_DIR)/apps/%.py $(BIN)/halide.so
	@echo Testing $* $<...
	@mkdir -p $(TEST_TMP)
	@cd $(TEST_TMP); PYTHONPATH="$(BIN):$$PYTHONPATH" $(PYTHON) $<

.PHONY: test_correctness
test_correctness: $(CORRECTNESS:$(ROOT_DIR)/correctness/%.py=test_correctness_%)

test_correctness_%: $(ROOT_DIR)/correctness/%.py $(BIN)/halide.so
	@echo Testing $* $<...
	@mkdir -p $(TEST_TMP)
	@cd $(TEST_TMP); PYTHONPATH="$(BIN):$$PYTHONPATH" $(PYTHON) $<

.PHONY: test_tutorial
test_tutorial: $(TUTORIAL:$(ROOT_DIR)/tutorial/%.py=test_tutorial_%)

test_tutorial_%: $(ROOT_DIR)/tutorial/%.py $(BIN)/halide.so
	@echo Testing $*...
	@mkdir -p $(TEST_TMP)
	@cd $(TEST_TMP); PYTHONPATH="$(BIN):$$PYTHONPATH" $(PYTHON) $<

clean:
	rm -rf $(BIN)

test: test_correctness test_apps test_tutorial

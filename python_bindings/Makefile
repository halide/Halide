UNAME = $(shell uname)
THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
ROOT_DIR = $(strip $(shell dirname $(THIS_MAKEFILE)))

# These are set by Halide's Makefile when built via that path.
HALIDE_PATH ?= $(ROOT_DIR)/..
HALIDE_DISTRIB_PATH ?= $(HALIDE_PATH)/distrib
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
	# Note that Python on OSX won't load .dylib; it requires .so
    SUFFIX=.so
else
    SHARED_EXT=so
endif
endif

LIBHALIDE ?= $(HALIDE_DISTRIB_PATH)/bin/libHalide.$(SHARED_EXT)

SUFFIX ?= .$(SHARED_EXT)
ifeq ($(PYTHON), python3)
    SUFFIX = $(shell $(PYTHON)-config --extension-suffix)
endif

# Discover PyBind path from `python3 -m pybind11 --includes`
# if it is pip/conda installed, which is a common case.
# Cf. https://github.com/pybind/pybind11/blob/master/docs/compiling.rst#building-manually
PYBIND11_CFLAGS = $(shell $(PYTHON) -m pybind11 --includes)
ifeq ($(PYBIND11_CFLAGS),)
    ifndef PYBIND11_PATH
        $(error PYBIND11_PATH is undefined)
    endif
    PYBIND11_PATH ?= /path/to/pybind11
    PYBIND11_CFLAGS = -I $(PYBIND11_PATH)/include
endif

OPTIMIZE ?= -O3

# defining DEBUG + undefining NDEBUG gives extra debug info in PyBind11
# OPTIMIZE ?= -g -DDEBUG=1 -UNDEBUG

# Compiling with -fvisibility=hidden saves ~80k on optimized x64 builds
CCFLAGS=$(shell $(PYTHON)-config --cflags) $(PYBIND11_CFLAGS) -I $(HALIDE_DISTRIB_PATH)/include -I $(ROOT_DIR) -std=c++11 $(FPIC) -fvisibility=hidden -fvisibility-inlines-hidden $(OPTIMIZE)
# Filter out a pointless warning present in some Python installs
CCFLAGS := $(filter-out -Wstrict-prototypes,$(CCFLAGS))

# DON'T link libpython* - leave those symbols to lazily resolve at load time
# Cf. https://github.com/pybind/pybind11/blob/master/docs/compiling.rst#building-manually
LDFLAGS=-lz
ifeq ($(UNAME), Darwin)
    # Keep OS X builds from complaining about missing libpython symbols
    LDFLAGS += -undefined dynamic_lookup
endif

PY_SRCS=$(shell ls $(ROOT_DIR)/src/*.cpp)
PY_OBJS=$(PY_SRCS:$(ROOT_DIR)/src/%.cpp=$(BIN)/src/%.o)

MODULE=$(BIN)/halide$(SUFFIX)

$(MODULE): $(PY_OBJS) $(LIBHALIDE)
	@mkdir -p $(@D)
	$(CXX) $^ $(LDFLAGS) -shared -o $@

# We don't want any of this auto-deleted
.SECONDARY:

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

# Compile the generators:
$(BIN)/%.gen: $(HALIDE_DISTRIB_PATH)/tools/GenGen.cpp correctness/%_generator.cpp $(LIBHALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CCFLAGS) $(LDFLAGS) $^ -o $@

# Special generator for generating a runtime:
$(BIN)/runtime.gen: $(HALIDE_DISTRIB_PATH)/tools/GenGen.cpp $(LIBHALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CCFLAGS) $(LDFLAGS) $^ -o $@

# Generate a runtime:
$(BIN)/runtime.a: $(BIN)/runtime.gen
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN) target=host

# Which target features to use for which test targets.
target_features_addconstant=-legacy_buffer_wrappers-no_runtime
target_features_bit=-no_runtime
target_features_user_context=-user_context-legacy_buffer_wrappers-no_runtime

# Make the generator generate a Python extension:
$(BIN)/%.py.c $(BIN)/%.cpp $(BIN)/%.h: $(BIN)/%.gen
	LD_LIBRARY_PATH=$(HALIDE_DISTRIB_PATH)/bin $< -e h,cpp,py.c \
	    -g $(notdir $(basename $<)) -o $(BIN) \
	    target=host$(target_features_$(notdir $(basename $<)))

# Compile the generated Python extension(s):
$(BIN)/%.o: $(BIN)/%.cpp
	$(CXX) -c $(FPIC) $(CCFLAGS) $^ -o $@
$(BIN)/%.py.o: $(BIN)/%.py.c
	$(CXX) -c $(FPIC) $(CCFLAGS) $^ -o $@

# The Python extension of the generator is already in $(BIN), and is named
# the same, so put the Python extension of the function into ext/.
# TODO: Python extensions of generators should have a _generator suffix.
$(BIN)/ext/%.so: $(BIN)/%.py.o $(BIN)/%.o $(BIN)/runtime.a
	@mkdir -p $(BIN)/ext
	$(CXX) $(LDFLAGS) $^ -shared -o $@

# Run the Python extension(s):
%.run: $(ROOT_DIR)/correctness/%_test.py $(BIN)/ext/%.so
	PYTHONPATH="$(BIN)/ext:$$PYTHONPATH" $(PYTHON) $<

# TODO: In the optimal case, we'd do %.run on all our generators. Unfortunately,
# every generator needs its own settings. See https://github.com/halide/Halide/issues/2977.
.PHONY: test_correctness_bit_test test_correctness_addconstant_test test_correctness_pystub test_correctness_user_context
test_correctness_addconstant_test: addconstant.run ;
test_correctness_bit_test: bit.run ;
test_correctness_user_context_test: user_context.run ;
test_correctness_pystub: $(BIN)/simplestub.so $(BIN)/complexstub.so $(BIN)/buildmethod.so $(BIN)/partialbuildmethod.so $(BIN)/nobuildmethod.so

APPS = $(shell ls $(ROOT_DIR)/apps/*.py)
CORRECTNESS = $(shell ls $(ROOT_DIR)/correctness/*.py)
TUTORIAL = $(shell ls $(ROOT_DIR)/tutorial/*.py)

.PHONY: test_apps
test_apps: $(APPS:$(ROOT_DIR)/apps/%.py=test_apps_%)

test_apps_%: $(ROOT_DIR)/apps/%.py $(MODULE)
	@echo Testing $*...
	@mkdir -p $(TEST_TMP)
	@# Send stdout (but not stderr) from these to /dev/null to reduce noise
	@cd $(TEST_TMP); PYTHONPATH="$(BIN):$$PYTHONPATH" $(PYTHON) $< >/dev/null

.PHONY: test_correctness
test_correctness: $(CORRECTNESS:$(ROOT_DIR)/correctness/%.py=test_correctness_%)

test_correctness_%: $(ROOT_DIR)/correctness/%.py $(MODULE)
	@echo Testing $*...
	@mkdir -p $(TEST_TMP)
	@cd $(TEST_TMP); PYTHONPATH="$(BIN):$$PYTHONPATH" $(PYTHON) $<

.PHONY: test_tutorial
test_tutorial: $(TUTORIAL:$(ROOT_DIR)/tutorial/%.py=test_tutorial_%)

test_tutorial_%: $(ROOT_DIR)/tutorial/%.py $(MODULE)
	@echo Testing $*...
	@mkdir -p $(TEST_TMP)
	@# Send stdout (but not stderr) from these to /dev/null to reduce noise
	@# We need "." in the PYTHONPATH for lesson_10_halide.so.
	@cd $(TEST_TMP); PYTHONPATH=".:$(BIN):$$PYTHONPATH" $(PYTHON) $< >/dev/null

test_tutorial_lesson_10_aot_compilation_run: $(TEST_TMP)/lesson_10_halide.so

$(TEST_TMP)/lesson_10_halide.so: test_tutorial_lesson_10_aot_compilation_generate
	$(CXX) $(CCFLAGS) $(LDFLAGS) $(FPIC) -shared \
		$(TEST_TMP)/lesson_10_halide.py.c \
		$(TEST_TMP)/lesson_10_halide.o \
		-I $(TEST_TMP) -o $@

clean:
	rm -rf $(BIN)

test: test_correctness test_apps test_tutorial

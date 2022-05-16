UNAME = $(shell uname)
THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
ROOT_DIR = $(strip $(shell dirname $(THIS_MAKEFILE)))

# These are set by Halide's Makefile when built via that path.
HALIDE_PATH ?= $(ROOT_DIR)/..
HALIDE_DISTRIB_PATH ?= $(HALIDE_PATH)/distrib
BIN ?= $(ROOT_DIR)/bin
PYTHON ?= python3
TEST_TMP ?= $(BIN)/tmp

FPIC=-fPIC
ifeq ($(UNAME), Darwin)
    SHARED_EXT=dylib
else
    SHARED_EXT=so
endif

ifeq ($(UNAME), Linux)
USE_EXPORT_DYNAMIC=-rdynamic
else
ifeq ($(UNAME), Darwin)
USE_EXPORT_DYNAMIC=-undefined dynamic_lookup
else
USE_EXPORT_DYNAMIC=
endif
endif

LIBHALIDE ?= $(HALIDE_DISTRIB_PATH)/lib/libHalide.$(SHARED_EXT)

SUFFIX = $(shell $(PYTHON)-config --extension-suffix)

# Discover PyBind path from `python3 -m pybind11 --includes`
PYBIND11_CFLAGS = $(shell $(PYTHON) -m pybind11 --includes)

OPTIMIZE ?= -O3

# defining DEBUG + undefining NDEBUG gives extra debug info in PyBind11
# OPTIMIZE ?= -g -DDEBUG=1 -UNDEBUG

# Compiling with -fvisibility=hidden saves ~80k on optimized x64 builds.
# It's critical to include -fno-omit-frame-pointer, otherwise introspection can
# break in amusing ways.
CCFLAGS=$(shell $(PYTHON)-config --cflags) $(PYBIND11_CFLAGS) -I $(HALIDE_DISTRIB_PATH)/include -I $(ROOT_DIR) -std=c++17 $(FPIC) -fvisibility=hidden -fvisibility-inlines-hidden -fno-omit-frame-pointer $(OPTIMIZE) $(CXXFLAGS)
# Filter out a pointless warning present in some Python installs
CCFLAGS := $(filter-out -Wstrict-prototypes,$(CCFLAGS))

# DON'T link libpython* - leave those symbols to lazily resolve at load time
# Cf. https://github.com/pybind/pybind11/blob/master/docs/compiling.rst#building-manually
LDFLAGS += -lz $(USE_EXPORT_DYNAMIC)
LDFLAGS += -Wl,-rpath,$(dir $(LIBHALIDE))

PY_SRCS=$(shell ls $(ROOT_DIR)/src/*.cpp)
PY_OBJS=$(PY_SRCS:$(ROOT_DIR)/src/%.cpp=$(BIN)/src/%.o)

MODULE=$(BIN)/halide$(SUFFIX)

$(MODULE): $(PY_OBJS) $(LIBHALIDE)
	@echo Building $@...
	@mkdir -p $(@D)
	@$(CXX) $^ $(LDFLAGS) -shared -o $@

# We don't want any of this auto-deleted
.SECONDARY:

$(BIN)/src/%.o: $(ROOT_DIR)/src/%.cpp
	@echo Building $@...
	@mkdir -p $(@D)
	@$(CXX) $(CCFLAGS) -c $< -o $@

$(BIN)/%_generator.o: $(ROOT_DIR)/correctness/generators/%_generator.cpp $(HALIDE_DISTRIB_PATH)/include/Halide.h
	@echo Building $@...
	@mkdir -p $(@D)
	@$(CXX) $(CCFLAGS) -c $< -o $@

# There are two sorts of Python Extensions that we can produce for a Halide Generator
# written in C++:
#
# - One that is essentially the 'native code' output of a Generator, wrapped with enough CPython
#   glue code to make it callable from Python. This is analogous to the usual Generator output
#   when building a C++ codebase, and is the usual mode used for distribution of final product;
#   these correspond to 'ahead-of-time' (AOT) code generation. The resulting code has no dependency
#   on libHalide. We'll refer to this sort of extension as an "AOT extension".
#
# - One that essentially *the Generator itself*, wrapped in CPython glue code to make it callable
#   from Python at Halide compilation time. This is analogous to the (rarely used) GeneratorStub
#   code that can be used to compose multiple Generators together. The resulting extension *does*
#   depend on libHalide, and can be used in either JIT or AOT mode for compilation.
#   We'll refer to this sort of extension as a "Stub extension".
#
# For testing purposes here, we don't bother using distutils/setuptools to produce a properly-packaged
# Python extension; rather, we simply produce a .so file with the correct name exported, and ensure
# it's in the PYTHONPATH when testing.
#
# In our build files here, we build both kinds of extension for every Generator in the generators/
# directory (even though not all are used). As a simplistic way to distinguish between the two
# sorts of extensions, we use the unadorned Generator name for AOT extensions, and the Generator name
# suffixed with "_stub" for Stub extensions. (TODO: this is unsatisfyingly hackish; better suggestions
# would be welcome.)

$(BIN)/PyStubImpl.o: $(ROOT_DIR)/stub/PyStubImpl.cpp $(HALIDE_DISTRIB_PATH)/include/Halide.h
	@echo Building $@...
	@mkdir -p $(@D)
	@$(CXX) $(CCFLAGS) -c $< -o $@

# Compile the generators:
$(BIN)/%.generator: $(HALIDE_DISTRIB_PATH)/tools/GenGen.cpp $(BIN)/%_generator.o $(LIBHALIDE)
	@echo Building $@...
	@mkdir -p $(@D)
	@$(CXX) $(CCFLAGS) $(LDFLAGS) $^ -o $@

# Special generator for generating a runtime:
$(BIN)/runtime.generator: $(HALIDE_DISTRIB_PATH)/tools/GenGen.cpp $(LIBHALIDE)
	@echo Building $@...
	@mkdir -p $(@D)
	@$(CXX) $(CCFLAGS) $(LDFLAGS) $^ -o $@

# Generate a runtime:
$(BIN)/runtime.a: $(BIN)/runtime.generator
	@echo Building $@...
	@mkdir -p $(@D)
	@$< -r runtime -o $(BIN) target=host

# Construct linker script that will export *just* the PyInit entry
# point we want. (If we don't do this we can have interesting failures
# when loading multiple of these Python extensions in the same space.)
ifeq ($(UNAME), Darwin)
$(BIN)/%.ldscript: $(ROOT_DIR)/stub/ext.ldscript.apple.in
	@echo Building $@...
	@mkdir -p $(@D)
	@cat $< | sed 's/$${SYMBOL}/$*/' > $@
PYEXT_LDSCRIPT_FLAG = -Wl,-exported_symbols_list %LDSCRIPT%
else
# Assume Desktop Linux
$(BIN)/%.ldscript: $(ROOT_DIR)/stub/ext.ldscript.linux.in
	@echo Building $@...
	@mkdir -p $(@D)
	@cat $< | sed 's/$${SYMBOL}/$*/' > $@
PYEXT_LDSCRIPT_FLAG = -Wl,--version-script=%LDSCRIPT%
endif

# Some Generators require extra Halide Target Features to be set.
FEATURES_user_context=-user_context

# Some Generators have undefined types, sizes, etc that are useful for Stubs extensions,
# but unacceptable for AOT Extensions; ensure that all of those are explicitly
# specified for AOT. (We currently don't use or test these in AOT form, so the settings
# are somewhat arbitrary.)
GENPARAMS_complex=\
	array_input.size=2 \
	array_input.type=uint8 \
	int_arg.size=2 \
	simple_input.type=uint8 \
	untyped_buffer_input.type=uint8 \
	untyped_buffer_output.type=uint8

GENPARAMS_simple=\
	func_input.type=uint8

# Run the Generator to produce a static library of AOT code,
# plus the 'python_extension' code necessary to produce a useful
# AOT Extention for Python:
$(BIN)/%.py.cpp $(BIN)/%.a $(BIN)/%.h: $(BIN)/%.generator
	@echo Building $@...
	@LD_LIBRARY_PATH=$(HALIDE_DISTRIB_PATH)/bin $< \
	    -e static_library,c_header,python_extension \
	    -g $(notdir $(basename $<)) \
	    -o $(BIN) \
	    target=host-no_runtime$(FEATURES_$(notdir $(basename $<))) \
	    $(GENPARAMS_$(notdir $(basename $<)))

# Compile the generated Python extension(s):
$(BIN)/%.py.o: $(BIN)/%.py.cpp
	@echo Building $@...
	@$(CXX) -c $(FPIC) $(CCFLAGS) $^ -o $@

# We take the native-code output of the Generator, add the Python-Extension
# code (to make it callable from Python), and put the resulting Python AOT Extension
# into the 'aot' folder.
$(BIN)/aot/%.so: $(BIN)/%.py.o $(BIN)/%.a $(BIN)/runtime.a $(BIN)/%.ldscript
	@echo Building $@...
	@mkdir -p $(@D)
	@$(CXX) $(LDFLAGS) $(filter-out %.ldscript,$^) -shared $(subst %LDSCRIPT%,$(BIN)/$*.ldscript,$(PYEXT_LDSCRIPT_FLAG)) -o $@

# OK, now we want to produce a Stub Extension for the same Generator:
# Compiling PyStub.cpp, then linking with the generator's .o file, PyStubImpl.o, plus the same libHalide
# being used by halide.so.
#
# Note that we set HALIDE_PYSTUB_MODULE_NAME to $*_stub (e.g. foo_stub) but
# set HALIDE_PYSTUB_GENERATOR_NAME to the unadorned name of the Generator.
$(BIN)/%_PyStub.o: $(ROOT_DIR)/stub/PyStub.cpp
	@echo Building $@...
	@mkdir -p $(@D)
	@$(CXX) $(CCFLAGS) -DHALIDE_PYSTUB_MODULE_NAME=$*_stub -DHALIDE_PYSTUB_GENERATOR_NAME=$* -c $< -o $@

$(BIN)/stub/%_stub.so: $(BIN)/%_PyStub.o $(BIN)/PyStubImpl.o $(BIN)/%_generator.o $(BIN)/%_stub.ldscript $(LIBHALIDE)
	@echo Building $@...
	@mkdir -p $(@D)
	@$(CXX) $(LDFLAGS) $(filter-out %.ldscript,$^) -shared $(subst %LDSCRIPT%,$(BIN)/$*_stub.ldscript,$(PYEXT_LDSCRIPT_FLAG)) -o $@

GENERATOR_SRCS=$(shell ls $(ROOT_DIR)/correctness/generators/*_generator.cpp)
GENERATOR_AOT_EXTENSIONS=$(GENERATOR_SRCS:$(ROOT_DIR)/correctness/generators/%_generator.cpp=$(BIN)/aot/%.so)
GENERATOR_STUB_EXTENSIONS=$(GENERATOR_SRCS:$(ROOT_DIR)/correctness/generators/%_generator.cpp=$(BIN)/stub/%_stub.so)

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

# For simplicity of the build system, we just have every correctness test depend
# on every Generator and AOT extension. Normally this would not be a good idea,
# but it's fine for us here.
test_correctness_%: $(ROOT_DIR)/correctness/%.py $(MODULE) $(GENERATOR_AOT_EXTENSIONS) $(GENERATOR_STUB_EXTENSIONS)
	@echo Testing $*...
	@mkdir -p $(TEST_TMP)
	@cd $(TEST_TMP); PYTHONPATH="$(BIN)/aot:$(BIN)/stub:$(BIN):$$PYTHONPATH" $(PYTHON) $<

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
	@echo Building $@...
	@$(CXX) $(CCFLAGS) $(LDFLAGS) $(FPIC) -shared \
		$(TEST_TMP)/lesson_10_halide.py.cpp \
		$(TEST_TMP)/lesson_10_halide.o \
		-I $(TEST_TMP) -o $@

.PHONY: clean
clean:
	rm -rf $(BIN)

.PHONY: test
test: test_correctness test_apps test_tutorial

# TODO(srj): the python bindings need to be put into the distrib folders;
# this is a hopefully-temporary workaround (https://github.com/halide/Halide/issues/4368)
.PHONY: build_python_bindings
build_python_bindings: $(MODULE)

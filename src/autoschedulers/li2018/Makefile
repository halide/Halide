THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
SRC  = $(strip $(shell dirname $(THIS_MAKEFILE)))
HALIDE_SRC_ROOT = $(realpath $(SRC)/../../../)
COMMON_DIR =  $(realpath $(SRC)/../common/)

# Assume an in-tree build of a halide distro exists. Most uses of this
# Makefile should probably set this variable explicitly.
HALIDE_DISTRIB_PATH ?= $(HALIDE_SRC_ROOT)/distrib

# The example uses a generator, though the autoscheduler itself does not require one
include $(HALIDE_SRC_ROOT)/apps/support/Makefile.inc

CXXFLAGS += -I$(COMMON_DIR)

ifeq ($(UNAME), Darwin)
HALIDE_RPATH_FOR_LIB += '-Wl,-rpath,@loader_path'
else
HALIDE_RPATH_FOR_LIB += '-Wl,-rpath,$$ORIGIN'
endif

$(BIN)/libautoschedule_li2018.$(SHARED_EXT): $(SRC)/GradientAutoscheduler.cpp $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC -fvisibility=hidden -fvisibility-inlines-hidden $(CXXFLAGS) $(OPTIMIZE) $^ -o $@ $(HALIDE_SYSTEM_LIBS) $(HALIDE_RPATH_FOR_LIB)

# Demonstrate a JIT-based use of gradient autoscheuler
$(BIN)/test: $(SRC)/test.cpp $(BIN)/libautoschedule_li2018.$(SHARED_EXT)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) $(SRC)/test.cpp -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# Demonstrate a generator-based use of gradient autoscheuler
$(GENERATOR_BIN)/demo.generator: $(SRC)/demo_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# Use the -p flag to the generator to load the autoscheduler as a plugin
$(BIN)/%/demo.a: $(GENERATOR_BIN)/demo.generator $(BIN)/libautoschedule_li2018.$(SHARED_EXT)
	@mkdir -p $(@D)
	$(GENERATOR_BIN)/demo.generator -g demo -o $(@D) -f demo target=$* auto_schedule=true -p $(BIN)/libautoschedule_li2018.$(SHARED_EXT) -s Li2018

$(BIN)/%/demo.rungen: $(BIN)/%/RunGenMain.o $(BIN)/%/demo.registration.cpp $(BIN)/%/demo.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $^ -o $@ $(HALIDE_SYSTEM_LIBS) $(IMAGE_IO_FLAGS)

.PHONY: build test clean run_test_cpp run_test_py test_generator

# demonstrates single-shot use of the autoscheduler
test_generator: $(BIN)/$(HL_TARGET)/demo.rungen $(BIN)/libautoschedule_li2018.$(SHARED_EXT)
	$< --benchmarks=all --benchmark_min_time=1 --estimate_all

run_test_cpp: $(BIN)/test
	LD_LIBRARY_PATH=$(BIN) $< $(BIN)/libautoschedule_li2018.$(SHARED_EXT)

run_test_py: $(SRC)/test.py $(BIN)/libautoschedule_li2018.$(SHARED_EXT)
	PYTHONPATH=$(BIN):$(HALIDE_PYTHON_BINDINGS_PATH):$(HALIDE_DISTRIB_PATH)/bin:$$PYTHONPATH \
		LD_LIBRARY_PATH=$(BIN):$(HALIDE_PYTHON_BINDINGS_PATH):$(HALIDE_DISTRIB_PATH)/bin \
		$(PYTHON) $(SRC)/test.py

\build: $(BIN)/test $(BIN)/$(HL_TARGET)/demo.rungen $(BIN)/libautoschedule_li2018.$(SHARED_EXT)

test: run_test_cpp run_test_py test_generator

clean:
	rm -rf $(BIN)

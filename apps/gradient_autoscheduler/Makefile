include ../support/Makefile.inc

WITH_PYTHON ?= 0
ifeq ($(WITH_PYTHON),1)
	PYTHON ?= python3
	# Discover pybind11 path from `python3 -m pybind11 --includes`
	# if it is pip/conda installed, which is a common case.
	# Cf. https://github.com/pybind/pybind11/blob/master/docs/compiling.rst#building-manually
	PYBIND11_CFLAGS = $(shell $(PYTHON) -m pybind11 --includes)
	ifeq ($(PYBIND11_CFLAGS),)
		ifndef PYBIND11_PATH
			$(error Can't find pybind11 for compiling the Python module.
				    Set the environment variable WITH_PYTHON=0 to disable Python support.)
		endif
		PYBIND11_PATH ?= /path/to/pybind11
		PYBIND11_CFLAGS = -I $(PYBIND11_PATH)/include
	endif
	PYTHON_FLAGS = -DWITH_PYTHON $(PYBIND11_CFLAGS) $(shell $(PYTHON)-config --cflags)
	# # DON'T link libpython* - leave those symbols to lazily resolve at load time
	# # Cf. https://github.com/pybind/pybind11/blob/master/docs/compiling.rst#building-manually
	ifeq ($(UNAME), Darwin)
	    # Keep OS X builds from complaining about missing libpython symbols
	    PYTHON_FLAGS += -undefined dynamic_lookup
	endif
endif

bin/libgradient_autoscheduler.so: GradientAutoscheduler.cpp ASLog.cpp
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC -fvisibility=hidden -fvisibility-inlines-hidden $(CXXFLAGS) -g $(OPTIMIZE) $^ -o $@ $(HALIDE_SYSTEM_LIBS) $(PYTHON_FLAGS)

# Demonstrate a JIT-based use of gradient autoscheuler
bin/test: test.cpp bin/libgradient_autoscheduler.so
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) test.cpp -o $@ $(LDFLAGS) $(LIB_HALIDE) $(HALIDE_SYSTEM_LIBS)

test: bin/test
	LD_LIBRARY_PATH=bin $<


# Demonstrate a generator-based use of gradient autoscheuler
$(GENERATOR_BIN)/demo.generator: demo_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# Use the -p flag to the generator to load the autoscheduler as a plugin
$(BIN)/%/demo.a: $(GENERATOR_BIN)/demo.generator bin/libgradient_autoscheduler.so
	@mkdir -p $(@D)
	$(GENERATOR_BIN)/demo.generator -g demo -o $(@D) -f demo target=$* auto_schedule=true -p bin/libgradient_autoscheduler.so -s Li2018

$(BIN)/%/demo.rungen: $(BIN)/%/RunGenMain.o $(BIN)/%/demo.registration.cpp $(BIN)/%/demo.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $^ -o $@ $(HALIDE_SYSTEM_LIBS) $(IMAGE_IO_FLAGS)

# demonstrates single-shot use of the autoscheduler
test_generator: $(BIN)/$(HL_TARGET)/demo.rungen bin/libgradient_autoscheduler.so
	$< --benchmarks=all --benchmark_min_time=1 --estimate_all


clean:
	rm -rf bin

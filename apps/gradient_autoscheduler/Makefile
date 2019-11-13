include ../support/Makefile.inc

$(BIN)/libgradient_autoscheduler.so: GradientAutoscheduler.cpp ASLog.cpp $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC -fvisibility=hidden -fvisibility-inlines-hidden $(CXXFLAGS) $(OPTIMIZE) $^ -o $@ $(HALIDE_SYSTEM_LIBS)

# Demonstrate a JIT-based use of gradient autoscheuler
$(BIN)/test: test.cpp $(BIN)/libgradient_autoscheduler.so
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) test.cpp -o $@ $(LDFLAGS) $(LIB_HALIDE) $(HALIDE_SYSTEM_LIBS)

# Demonstrate a generator-based use of gradient autoscheuler
$(GENERATOR_BIN)/demo.generator: demo_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# Use the -p flag to the generator to load the autoscheduler as a plugin
$(BIN)/%/demo.a: $(GENERATOR_BIN)/demo.generator $(BIN)/libgradient_autoscheduler.so
	@mkdir -p $(@D)
	$(GENERATOR_BIN)/demo.generator -g demo -o $(@D) -f demo target=$* auto_schedule=true -p $(BIN)/libgradient_autoscheduler.so -s Li2018

$(BIN)/%/demo.rungen: $(BIN)/%/RunGenMain.o $(BIN)/%/demo.registration.cpp $(BIN)/%/demo.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $^ -o $@ $(HALIDE_SYSTEM_LIBS) $(IMAGE_IO_FLAGS)

.PHONY: test clean run_test_cpp run_test_py test_generator

# demonstrates single-shot use of the autoscheduler
test_generator: $(BIN)/$(HL_TARGET)/demo.rungen $(BIN)/libgradient_autoscheduler.so
	$< --benchmarks=all --benchmark_min_time=1 --estimate_all

run_test_cpp: $(BIN)/test
	LD_LIBRARY_PATH=$(BIN) $<

run_test_py: test.py $(BIN)/libgradient_autoscheduler.so
	PYTHONPATH=$(BIN):$(HALIDE_PYTHON_BINDINGS_PATH):$(HALIDE_DISTRIB_PATH)/bin:$$PYTHONPATH \
		LD_LIBRARY_PATH=$(BIN):$(HALIDE_PYTHON_BINDINGS_PATH):$(HALIDE_DISTRIB_PATH)/bin \
		$(PYTHON) test.py

test: run_test_cpp run_test_py test_generator

clean:
	rm -rf $(BIN)

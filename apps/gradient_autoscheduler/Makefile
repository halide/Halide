include ../support/Makefile.inc

bin/libgradient_autoscheduler.so: GradientAutoscheduler.cpp
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC $(CXXFLAGS) -g $(OPTIMIZE) $^ -o $@ $(HALIDE_SYSTEM_LIBS)

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
	$(GENERATOR_BIN)/demo.generator -g demo -o $(@D) -f demo target=$* auto_schedule=true -p bin/libgradient_autoscheduler.so

$(BIN)/%/demo.rungen: $(BIN)/%/RunGenMain.o $(BIN)/%/demo.registration.cpp $(BIN)/%/demo.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $^ -o $@ $(HALIDE_SYSTEM_LIBS) $(IMAGE_IO_FLAGS)

# demonstrates single-shot use of the autoscheduler
test_generator: $(BIN)/$(HL_TARGET)/demo.rungen bin/libgradient_autoscheduler.so
	$< --benchmarks=all --benchmark_min_time=1 --estimate_all


clean:
	rm -rf bin
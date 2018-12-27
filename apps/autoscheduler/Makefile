include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin
HL_TARGET ?= host

# A sample generator to autoschedule. Note that if it statically links
# to libHalide, then it must be build with $(USE_EXPORT_DYNAMIC), or the
# autoscheduler can't find the libHalide symbols that it needs.
$(BIN)/demo.generator: demo_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# To use the autoscheduler, set a few environment variables and use the -p flag to the generator to load the autoscheduler as a plugin
$(BIN)/demo.a: $(BIN)/demo.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	HL_MACHINE_PARAMS=32,1,1 HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=weights \
	$(BIN)/demo.generator -g demo -o $(BIN) -f demo target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so

$(BIN)/demo.rungen: $(BIN)/RunGenMain.o $(BIN)/demo.registration.cpp $(BIN)/demo.a
	$(CXX) -I $(BIN) $^ -o $@ $(HALIDE_SYSTEM_LIBS) $(IMAGE_IO_FLAGS)

# demonstrates single-shot use of the autoscheduler
demo: $(BIN)/demo.rungen
	$(BIN)/demo.rungen --benchmarks=all --benchmark_min_time=1 --default_input_buffers=random:0:auto --output_extents=estimate --default_input_scalars=estimate

# demonstrates an autotuning loop
autotune: $(BIN)/demo.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model autotune_loop.sh $(AUTOSCHED_BIN)/libauto_schedule.so
	bash autotune_loop.sh $(BIN)/demo.generator demo x86-64-avx2-disable_llvm_loop_unroll-disable_llvm_loop_vectorize

# Simple jit-based test
$(BIN)/test: test.cpp $(AUTOSCHED_BIN)/libauto_schedule.so
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) $< -o $@ $(LDFLAGS) $(LIB_HALIDE) $(HALIDE_SYSTEM_LIBS)

test: $(BIN)/test
	HL_WEIGHTS_DIR=weights LD_LIBRARY_PATH=$(BIN) $(BIN)/test

clean:
	rm -rf $(BIN)



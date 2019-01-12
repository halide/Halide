include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin

$(BIN)/fit_and_slice_3x4.generator: fit_and_slice_3x4_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/fit_and_slice_3x4.a: $(BIN)/fit_and_slice_3x4.generator
	@mkdir -p $(@D)
	$< -g fit_and_slice_3x4 -f fit_and_slice_3x4 -o $(BIN) target=$(HL_TARGET) auto_schedule=false

$(BIN)/fit_and_slice_3x4_classic_auto_schedule.a: $(BIN)/fit_and_slice_3x4.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	$< -g fit_and_slice_3x4 -f fit_and_slice_3x4_classic_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true machine_params=$(HL_MACHINE_PARAMS)

$(BIN)/fit_and_slice_3x4_auto_schedule.a: $(BIN)/fit_and_slice_3x4.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=$(AUTOSCHED_WEIGHTS_DIR) \
	$< -g fit_and_slice_3x4 -f fit_and_slice_3x4_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so

$(BIN)/process: process.cpp $(BIN)/fit_and_slice_3x4.a $(BIN)/fit_and_slice_3x4_auto_schedule.a $(BIN)/fit_and_slice_3x4_classic_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)

test: $(BIN)/process
	$(BIN)/process

autotune: $(BIN)/fit_and_slice_3x4.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/fit_and_slice_3x4.generator \
		fit_and_slice_3x4 \
		x86-64-avx2 \
		$(AUTOSCHED_WEIGHTS_DIR) \
		$(AUTOSCHED_BIN)

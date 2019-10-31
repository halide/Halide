include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

BIN ?= bin

$(BIN)/fit_and_slice_3x4.generator: fit_and_slice_3x4_generator.cpp ../autoscheduler/SimpleAutoSchedule.h ../autoscheduler/SimpleAutoSchedule.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/fit_and_slice_3x4.a: $(BIN)/fit_and_slice_3x4.generator
	@mkdir -p $(@D)
	$< -g fit_and_slice_3x4 -f fit_and_slice_3x4 -o $(BIN) target=$(HL_TARGET) auto_schedule=false

$(BIN)/fit_and_slice_3x4_classic_auto_schedule.a: $(BIN)/fit_and_slice_3x4.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	HL_COMPUTE_ROOT_AND_INLINE_ONLY=1 \
	$< -g fit_and_slice_3x4 -f fit_and_slice_3x4_classic_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) -e static_library,h,assembly,stmt 2> stderr_classic.txt > stdout_classic.txt

$(BIN)/fit_and_slice_3x4_auto_schedule.a: $(BIN)/fit_and_slice_3x4.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$< -g fit_and_slice_3x4 -f fit_and_slice_3x4_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) 2> stderr.txt > stdout.txt

$(BIN)/fit_and_slice_3x4_simple_auto_schedule.a: $(BIN)/fit_and_slice_3x4.generator
	@mkdir -p $(@D)
	HL_USE_SIMPLE_AUTOSCHEDULER=1 \
	$< -g fit_and_slice_3x4 -o $(BIN) -f fit_and_slice_3x4_simple_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=false -e static_library,h

$(BIN)/process: process.cpp $(BIN)/fit_and_slice_3x4.a $(BIN)/fit_and_slice_3x4_auto_schedule.a $(BIN)/fit_and_slice_3x4_classic_auto_schedule.a $(BIN)/fit_and_slice_3x4_simple_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)

test: $(BIN)/process
	$(BIN)/process

autotune: $(BIN)/fit_and_slice_3x4.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/retrain_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/fit_and_slice_3x4.generator \
		fit_and_slice_3x4 \
		$(HL_TARGET) \
		$(AUTOSCHED_SRC)/gpu.weights \
		$(AUTOSCHED_BIN)

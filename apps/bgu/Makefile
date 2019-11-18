include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

GRADIENT_AUTOSCHED_BIN=../gradient_autoscheduler/bin
CXXFLAGS += -I../support/

BIN ?= bin

$(GENERATOR_BIN)/fit_and_slice_3x4.generator: fit_and_slice_3x4_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/%/fit_and_slice_3x4.a: $(GENERATOR_BIN)/fit_and_slice_3x4.generator
	@mkdir -p $(@D)
	$< -g fit_and_slice_3x4 -f fit_and_slice_3x4 -e $(GENERATOR_OUTPUTS) -o $(@D) target=$* auto_schedule=false

$(BIN)/%/fit_and_slice_3x4_auto_schedule.a: $(GENERATOR_BIN)/fit_and_slice_3x4.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$< -g fit_and_slice_3x4 -f fit_and_slice_3x4_auto_schedule -e $(GENERATOR_OUTPUTS) -o $(@D) target=$* auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so -s Adams2019 machine_params=$(HL_MACHINE_PARAMS) 2> stderr.txt > stdout.txt

$(BIN)/%/fit_and_slice_3x4_gradient_auto_schedule.a: $(GENERATOR_BIN)/fit_and_slice_3x4.generator
	@mkdir -p $(@D)
	$^ -g fit_and_slice_3x4 -f fit_and_slice_3x4_gradient_auto_schedule -e $(GENERATOR_OUTPUTS) -o $(@D) target=$*-no_runtime auto_schedule=true -p $(GRADIENT_AUTOSCHED_BIN)/libgradient_autoscheduler.so -s Li2018

$(BIN)/%/process: process.cpp $(BIN)/%/fit_and_slice_3x4.a $(BIN)/%/fit_and_slice_3x4_auto_schedule.a $(BIN)/%/fit_and_slice_3x4_gradient_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/process
	$<

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
SAMPLES_DIR ?= autotuned_samples
autotune: $(GENERATOR_BIN)/fit_and_slice_3x4.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/retrain_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_MACHINE_PARAMS=80,1,1 \
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/fit_and_slice_3x4.generator \
		fit_and_slice_3x4 \
		host-cuda \
		$(AUTOSCHED_SRC)/gpu.weights \
		$(AUTOSCHED_BIN) \
		$(BATCH_ID) \
		$(TRAIN_ONLY)

include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

GRADIENT_AUTOSCHED_BIN=../gradient_autoscheduler/bin
CXXFLAGS += -I../support/

all: $(BIN)/$(HL_TARGET)/test

$(GENERATOR_BIN)/unsharp.generator: unsharp_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/%/unsharp.a: $(GENERATOR_BIN)/unsharp.generator
	@mkdir -p $(@D)
	$< -g unsharp -f unsharp -o $(BIN) target=$(HL_TARGET) auto_schedule=false

$(BIN)/%/unsharp_auto_schedule.a: $(GENERATOR_BIN)/unsharp.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$< -g unsharp -e $(GENERATOR_OUTPUTS) -o $(@D) -f unsharp_auto_schedule target=$(HL_TARGET) auto_schedule=true machine_params=$(HL_MACHINE_PARAMS) -p $(AUTOSCHED_BIN)/libauto_schedule.so -s Adams2019 2> stderr.txt > stdout.txt

$(BIN)/%/unsharp_gradient_auto_schedule.a: $(GENERATOR_BIN)/unsharp.generator
	@mkdir -p $(@D)
	$^ -g unsharp -e $(GENERATOR_OUTPUTS) -o $(@D) -f unsharp_gradient_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true -p $(GRADIENT_AUTOSCHED_BIN)/libgradient_autoscheduler.so -s Li2018

$(BIN)/%/filter: filter.cpp $(BIN)/%/unsharp.a $(BIN)/%/unsharp_auto_schedule.a $(BIN)/%/unsharp_gradient_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)

$(BIN)/%/out.tiff: $(BIN)/%/filter
	$< ../images/rgb.png $@

test: $(BIN)/$(HL_TARGET)/out.tiff

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
SAMPLES_DIR ?= autotuned_samples
autotune: $(GENERATOR_BIN)/unsharp.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/retrain_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_TARGET=host-cuda \
	HL_MACHINE_PARAMS=80,1,1 \
	SAMPLES_DIR=$(SAMPLES_DIR) \
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/unsharp.generator \
		unsharp \
		host-cuda \
		$(AUTOSCHED_SRC)/gpu.weights \
		$(AUTOSCHED_BIN) \
		$(BATCH_ID) \
		$(TRAIN_ONLY)

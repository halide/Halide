include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

GRADIENT_AUTOSCHED_BIN=../gradient_autoscheduler/bin
CXXFLAGS += -I../support/

FILTER_DEPS=$(BIN)/%/unsharp.a

ifndef NO_AUTO_SCHEDULE
	FILTER_DEPS += $(BIN)/%/unsharp_auto_schedule.a $(BIN)/%/unsharp_gradient_auto_schedule.a
else
	CXXFLAGS += -DNO_AUTO_SCHEDULE
endif

all: $(BIN)/$(HL_TARGET)/test

$(GENERATOR_BIN)/unsharp.generator: unsharp_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(GENERATOR_LDFLAGS)

$(BIN)/%/unsharp.a: $(GENERATOR_BIN)/unsharp.generator
	@mkdir -p $(@D)
	$< -g unsharp -f unsharp -o $(@D) target=$*-no_runtime auto_schedule=false

$(BIN)/%/unsharp_auto_schedule.a: $(GENERATOR_BIN)/unsharp.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$< -g unsharp -e $(GENERATOR_OUTPUTS) -o $(@D) -f unsharp_auto_schedule target=$* auto_schedule=true machine_params=$(HL_MACHINE_PARAMS) -p $(AUTOSCHED_BIN)/libauto_schedule.so -s Adams2019 2> stderr.txt > stdout.txt

$(BIN)/%/unsharp_gradient_auto_schedule.a: $(GENERATOR_BIN)/unsharp.generator
	@mkdir -p $(@D)
	$^ -g unsharp -e $(GENERATOR_OUTPUTS) -o $(@D) -f unsharp_gradient_auto_schedule target=$*-no_runtime auto_schedule=true -p $(GRADIENT_AUTOSCHED_BIN)/libgradient_autoscheduler.so -s Li2018

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/unsharp.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=$*

$(BIN)/%/filter: filter.cpp $(FILTER_DEPS) $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	echo $(CXXFLAGS)
	$(CXX) $(CXXFLAGS) -I$(@D) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< ../images/rgba.png $@
	rm $@

test: $(BIN)/$(HL_TARGET)/out.png

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
SAMPLES_DIR ?= autotuned_samples
autotune: $(GENERATOR_BIN)/unsharp.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/retrain_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_MACHINE_PARAMS=80,1,1 \
	SAMPLES_DIR=$(SAMPLES_DIR) \
	HL_PERMIT_FAILED_UNROLL=1 \
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

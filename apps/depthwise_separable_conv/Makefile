include ../support/Makefile.inc
include ../support/autoscheduler.inc

GRADIENT_AUTOSCHED_BIN=../gradient_autoscheduler/bin
CXXFLAGS += -I../support/

PROCESS_DEPS=$(BIN)/%/depthwise_separable_conv.a

ifndef NO_AUTO_SCHEDULE
	PROCESS_DEPS += $(BIN)/%/depthwise_separable_conv_auto_schedule.a $(BIN)/%/depthwise_separable_conv_gradient_auto_schedule.a
else
	CXXFLAGS += -DNO_AUTO_SCHEDULE
endif

all: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/depthwise_separable_conv.generator: depthwise_separable_conv_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/%/depthwise_separable_conv.a: $(GENERATOR_BIN)/depthwise_separable_conv.generator
	@mkdir -p $(@D)
	$^ -g depthwise_separable_conv -e $(GENERATOR_OUTPUTS) -o $(@D) -f depthwise_separable_conv target=$* auto_schedule=false

$(BIN)/%/depthwise_separable_conv_auto_schedule.a: $(GENERATOR_BIN)/depthwise_separable_conv.generator
	@mkdir -p $(@D)
	$^ -g depthwise_separable_conv -e $(GENERATOR_OUTPUTS) -o $(@D) -f depthwise_separable_conv_auto_schedule target=$*-no_runtime auto_schedule=true

$(BIN)/%/depthwise_separable_conv_auto_schedule.a: $(GENERATOR_BIN)/depthwise_separable_conv.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$< -g depthwise_separable_conv -f depthwise_separable_conv_auto_schedule -e $(GENERATOR_OUTPUTS) -o $(@D) target=$* auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so -s Adams2019 machine_params=$(HL_MACHINE_PARAMS) 2> stderr.txt > stdout.txt

$(BIN)/%/depthwise_separable_conv_gradient_auto_schedule.a: $(GENERATOR_BIN)/depthwise_separable_conv.generator
	@mkdir -p $(@D)
	$^ -g depthwise_separable_conv -f depthwise_separable_conv_gradient_auto_schedule -e $(GENERATOR_OUTPUTS) -o $(@D) target=$*-no_runtime auto_schedule=true -p $(GRADIENT_AUTOSCHED_BIN)/libgradient_autoscheduler.so -s Li2018

$(BIN)/%/process: process.cpp $(PROCESS_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS)

run: $(BIN)/$(HL_TARGET)/process
	@mkdir -p $(@D)
	$^

clean:
	rm -rf $(BIN)

test: run

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
SAMPLES_DIR ?= autotuned_samples
autotune: $(GENERATOR_BIN)/depthwise_separable_conv.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/retrain_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_MACHINE_PARAMS=80,1,1 \
	SAMPLES_DIR=$(SAMPLES_DIR) \
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/depthwise_separable_conv.generator \
		depthwise_separable_conv \
		$(HL_TARGET) \
		$(AUTOSCHED_SRC)/gpu.weights \
		$(AUTOSCHED_BIN) \
		$(BATCH_ID) \
		$(TRAIN_ONLY)

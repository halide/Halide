include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

GRADIENT_AUTOSCHED_BIN=../gradient_autoscheduler/bin
CXXFLAGS += -I../support/

PROCESS_DEPS=$(BIN)/%/nl_means.a

ifndef NO_AUTO_SCHEDULE
	PROCESS_DEPS += $(BIN)/%/nl_means_auto_schedule.a $(BIN)/%/nl_means_gradient_auto_schedule.a
else
	CXXFLAGS += -DNO_AUTO_SCHEDULE
endif

all: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/nl_means.generator: nl_means_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(GENERATOR_LDFLAGS)

$(BIN)/%/nl_means.a: $(GENERATOR_BIN)/nl_means.generator
	@mkdir -p $(@D)
	$^ -g nl_means -e $(GENERATOR_OUTPUTS) -o $(@D) -f nl_means target=$* auto_schedule=false

$(BIN)/%/nl_means_auto_schedule.a: $(GENERATOR_BIN)/nl_means.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$< -g nl_means -e $(GENERATOR_OUTPUTS) -o $(@D) -f nl_means_auto_schedule target=$*-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so -s Adams2019 machine_params=$(HL_MACHINE_PARAMS) 2> stderr.txt > stdout.txt

$(BIN)/%/nl_means_gradient_auto_schedule.a: $(GENERATOR_BIN)/nl_means.generator
	@mkdir -p $(@D)
	$^ -g nl_means -e $(GENERATOR_OUTPUTS) -o $(@D) -f nl_means_gradient_auto_schedule target=$*-no_runtime auto_schedule=true -p $(GRADIENT_AUTOSCHED_BIN)/libgradient_autoscheduler.so -s Li2018

$(BIN)/%/process: process.cpp $(PROCESS_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(@D) -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb.png 7 7 0.12 10 $@
	rm $@

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
SAMPLES_DIR ?= autotuned_samples
autotune: $(GENERATOR_BIN)/nl_means.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/retrain_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_MACHINE_PARAMS=80,1,1 \
	SAMPLES_DIR=$(SAMPLES_DIR) \
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/nl_means.generator \
		nl_means \
		host-cuda \
		$(AUTOSCHED_SRC)/gpu.weights \
		$(AUTOSCHED_BIN) \
		$(BATCH_ID) \
		$(TRAIN_ONLY)

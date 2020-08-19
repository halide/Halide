include ../support/Makefile.inc
include ../support/autoscheduler.inc

.PHONY: build clean test

build: $(BIN)/$(HL_TARGET)/process

GRADIENT_AUTOSCHED_BIN=../gradient_autoscheduler/bin
CXXFLAGS += -I../support/

BIN ?= bin

PROCESS_DEPS=$(BIN)/%/basic_demosaic.a

ifndef NO_GRADIENT_AUTO_SCHEDULE
	PROCESS_DEPS += $(BIN)/%/basic_demosaic_gradient_auto_schedule.a
else
	CXXFLAGS += -DNO_GRADIENT_AUTO_SCHEDULE
endif

ifndef NO_AUTO_SCHEDULE
	PROCESS_DEPS += $(BIN)/%/basic_demosaic_auto_schedule_sioutas.a $(BIN)/%/basic_demosaic_auto_schedule_sioutas_folded.a
else
	CXXFLAGS += -DNO_AUTO_SCHEDULE
endif

$(GENERATOR_BIN)/basic_demosaic.generator: basic_demosaic_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	@echo $(CXXFLAGS)
	@echo $(CXX) cxx
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS) 

$(BIN)/%/basic_demosaic.a: $(GENERATOR_BIN)/basic_demosaic.generator
	@mkdir -p $(@D)
	$^ -g basic_demosaic -e $(GENERATOR_OUTPUTS) -o $(@D) -f basic_demosaic target=$* auto_schedule=false

$(BIN)/%/basic_demosaic_auto_schedule_sioutas_folded.a: $(GENERATOR_BIN)/basic_demosaic.generator
	@mkdir -p $(@D)
	HL_AUTO_FOLDED_FUSION=1 \
	$^ -g basic_demosaic -e $(GENERATOR_OUTPUTS) -o $(@D) -f basic_demosaic_auto_schedule_sioutas_folded target=$*-no_runtime auto_schedule=true -p ../sioutas2020/bin/libauto_schedule.so -s Sioutas2020

$(BIN)/%/basic_demosaic_auto_schedule_sioutas.a: $(GENERATOR_BIN)/basic_demosaic.generator
	@mkdir -p $(@D)
	$^ -g basic_demosaic -e $(GENERATOR_OUTPUTS) -o $(@D) -f basic_demosaic_auto_schedule_sioutas target=$*-no_runtime auto_schedule=true -p ../sioutas2020/bin/libauto_schedule.so -s Sioutas2020

$(BIN)/%/basic_demosaic_gradient_auto_schedule.a: $(GENERATOR_BIN)/basic_demosaic.generator
	@mkdir -p $(@D)
	$^ -g basic_demosaic -e $(GENERATOR_OUTPUTS) -o $(@D) -f basic_demosaic_gradient_auto_schedule target=$*-no_runtime auto_schedule=true -p $(GRADIENT_AUTOSCHED_BIN)/libgradient_autoscheduler.so -s Li2018

$(BIN)/%/process: process.cpp $(PROCESS_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(@D) -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$< 10

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
SAMPLES_DIR ?= autotuned_samples
autotune: $(GENERATOR_BIN)/basic_demosaic.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/retrain_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_MACHINE_PARAMS=80,1,1 \
	SAMPLES_DIR=$(SAMPLES_DIR) \
	HL_DEBUG_CODEGEN=0 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/basic_demosaic.generator \
		basic_demosaic \
		$(HL_TARGET) \
		$(AUTOSCHED_SRC)/gpu.weights \
		$(AUTOSCHED_BIN) \
		$(BATCH_ID) \
		$(TRAIN_ONLY)

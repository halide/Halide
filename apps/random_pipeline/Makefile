include ../support/Makefile.inc
include ../support/autoscheduler.inc

all: $(BIN)/test

PIPELINE_SEED ?= 0
PIPELINE_STAGES ?= 20
HL_RANDOM_DROPOUT ?= 100
HL_SEED ?= 0
HL_BEAM_SIZE ?= 1
HL_USE_MANUAL_COST_MODEL ?= 0

ifeq ($(NEW_AUTOSCHEDULER),1)
ID = $(HL_TARGET)-new_autoscheduler/pipeline_$(PIPELINE_SEED)_$(PIPELINE_STAGES)/schedule_$(HL_SEED)_$(HL_RANDOM_DROPOUT)_$(HL_BEAM_SIZE)_$(HL_USE_MANUAL_COST_MODEL)
GEN_GEN_ARGS = -p bin/libauto_schedule.so
else
ID = $(HL_TARGET)-$(CACHE)-$(BALANCE)/pipeline_$(PIPELINE_SEED)_$(PIPELINE_STAGES)/schedule_$(HL_SEED)_$(HL_RANDOM_DROPOUT)_$(HL_BEAM_SIZE)_$(HL_USE_MANUAL_COST_MODEL)
GEN_GEN_ARGS =
endif

ifeq ($(HL_SEED), root)
AUTO_SCHEDULE=false
else
AUTO_SCHEDULE=true
endif

$(BIN)/runtime.a: $(BIN)/random_pipeline.generator
	$^ -r runtime target=$(HL_TARGET) -o $(BIN)

$(BIN)/random_pipeline.generator: random_pipeline_generator.cpp ../autoscheduler/SimpleAutoSchedule.h ../autoscheduler/SimpleAutoSchedule.cpp ../autoscheduler/DerivativeUtils.h ../autoscheduler/DerivativeUtils.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/$(ID)/random_pipeline.a: $(BIN)/random_pipeline.generator
	@mkdir -p $(@D)
	HL_SHARED_MEMORY_LIMIT=48 \
	HL_RANDOM_DROPOUT=$(HL_RANDOM_DROPOUT) \
	HL_SEED=$(HL_SEED) \
	HL_BEAM_SIZE=$(HL_BEAM_SIZE) \
	HL_USE_MANUAL_COST_MODEL=$(HL_USE_MANUAL_COST_MODEL) \
	HL_DEBUG_CODEGEN=1 \
	HL_FEATURE_FILE=$(BIN)/$(ID)/binary_features.bin \
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(PWD)/../autoscheduler/gpu.weights \
	$^ -g random_pipeline -o $(BIN)/$(ID) -f random_pipeline target=$(HL_TARGET)-no_runtime auto_schedule=$(AUTO_SCHEDULE) seed=$(PIPELINE_SEED) max_stages=$(PIPELINE_STAGES) $(GEN_GEN_ARGS) 2> $(BIN)/$(ID)/stderr.txt > $(BIN)/$(ID)/stdout.txt

$(BIN)/$(ID)/random_pipeline_simple_auto_schedule.a: $(BIN)/random_pipeline.generator
	@mkdir -p $(@D)
	HL_USE_SIMPLE_AUTOSCHEDULER=1 \
	$< -g random_pipeline -o $(BIN)/$(ID) -f random_pipeline_simple_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=false -e static_library,h seed=$(PIPELINE_SEED) max_stages=$(PIPELINE_STAGES)

$(BIN)/$(ID)/test: test.cpp $(BIN)/$(ID)/random_pipeline.a $(BIN)/$(ID)/random_pipeline_simple_auto_schedule.a $(BIN)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$(ID) -Wall -O3 $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN)

$(BIN)/$(ID)/times.txt: $(BIN)/$(ID)/test
	$(BIN)/$(ID)/test 0 > $(BIN)/$(ID)/times.txt

$(BIN)/$(ID)/times_simple_autoschedule.txt: $(BIN)/$(ID)/test
	$(BIN)/$(ID)/test 1 > $(BIN)/$(ID)/times_simple_autoschedule.txt

build: $(BIN)/$(ID)/test
bench: $(BIN)/$(ID)/times.txt
bench_simple_autoschedule: $(BIN)/$(ID)/times_simple_autoschedule.txt

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
autotune_gpu: $(BIN)/random_pipeline.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/retrain_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_TARGET=host-cuda \
	HL_MACHINE_PARAMS=80,1,1 \
	SAMPLES_DIR=autotuned_samples \
	HL_SHARED_MEMORY_LIMIT=48 \
	HL_DEBUG_CODEGEN=1 \
	HL_WEIGHTS_DIR=$(PWD)/../autoscheduler/gpu.weights \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/random_pipeline.generator \
		random_pipeline \
		host-cuda \
		$(AUTOSCHED_SRC)/gpu.weights \
		$(AUTOSCHED_BIN) \
		$(BATCH_ID) \
		$(TRAIN_ONLY) \
		"seed=$(PIPELINE_SEED);max_stages=$(PIPELINE_STAGES)"

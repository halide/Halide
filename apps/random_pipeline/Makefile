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
ID = $(HL_TARGET)/pipeline_$(PIPELINE_SEED)_$(PIPELINE_STAGES)/schedule_$(HL_SEED)_$(HL_RANDOM_DROPOUT)_$(HL_BEAM_SIZE)_$(HL_USE_MANUAL_COST_MODEL)
GEN_GEN_ARGS =
endif

ifeq ($(HL_SEED), root)
AUTO_SCHEDULE=false
else
AUTO_SCHEDULE=true
endif

$(BIN)/runtime.a: $(BIN)/random_pipeline.generator
	$^ -r runtime target=$(HL_TARGET) -o $(BIN)

$(BIN)/random_pipeline.generator: random_pipeline_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/$(ID)/random_pipeline.a: $(BIN)/random_pipeline.generator
	@mkdir -p $(@D)
	HL_RANDOM_DROPOUT=$(HL_RANDOM_DROPOUT) \
	HL_SEED=$(HL_SEED) \
	HL_BEAM_SIZE=$(HL_BEAM_SIZE) \
	HL_USE_MANUAL_COST_MODEL=$(HL_USE_MANUAL_COST_MODEL) \
	HL_DEBUG_CODEGEN=1 \
	HL_FEATURE_FILE=$(BIN)/$(ID)/binary_features.bin \
	HL_PERMIT_FAILED_UNROLL=1 \
	$^ -g random_pipeline -o $(BIN)/$(ID) -f random_pipeline target=$(HL_TARGET)-no_runtime auto_schedule=$(AUTO_SCHEDULE) seed=$(PIPELINE_SEED) max_stages=$(PIPELINE_STAGES) $(GEN_GEN_ARGS) 2> $(BIN)/$(ID)/stderr.txt > $(BIN)/$(ID)/stdout.txt

$(BIN)/$(ID)/test: test.cpp $(BIN)/$(ID)/random_pipeline.a $(BIN)/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$(ID) -Wall -O3 $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN)

$(BIN)/$(ID)/times.txt: $(BIN)/$(ID)/test
	$(BIN)/$(ID)/test > $(BIN)/$(ID)/times.txt

build: $(BIN)/$(ID)/test
bench: $(BIN)/$(ID)/times.txt

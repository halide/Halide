include ../support/Makefile.inc

all: $(BIN)/test

PIPELINE_SEED ?=0
HL_RANDOM_DROPOUT ?=100
HL_SEED ?=0
HL_BEAM_SIZE ?=1

ID = $(HL_TARGET)/pipeline_$(PIPELINE_SEED)/schedule_$(HL_SEED)_$(HL_RANDOM_DROPOUT)_$(HL_BEAM_SIZE)

$(BIN)/random_pipeline.generator: random_pipeline_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/$(ID)/random_pipeline.a: $(BIN)/random_pipeline.generator
	@mkdir -p $(@D)
	$^ -g random_pipeline -o $(BIN)/$(ID) -f random_pipeline target=$(HL_TARGET) seed=$(PIPELINE_SEED)

$(BIN)/$(ID)/random_pipeline_auto_schedule.a: $(BIN)/random_pipeline.generator
	@mkdir -p $(@D)
	HL_RANDOM_DROPOUT=$(HL_RANDOM_DROPOUT) \
	HL_SEED=$(HL_SEED) \
	HL_BEAM_SIZE=$(HL_BEAM_SIZE) \
	$^ -g random_pipeline -o $(BIN)/$(ID) -f random_pipeline_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true seed=$(PIPELINE_SEED) 2> $(BIN)/$(ID)/stderr.txt > $(BIN)/$(ID)/stdout.txt

$(BIN)/$(ID)/test: test.cpp $(BIN)/$(ID)/random_pipeline.a $(BIN)/$(ID)/random_pipeline_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$(ID) -Wall -O3 $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN)

$(BIN)/$(ID)/times.txt: $(BIN)/$(ID)/test
	$(BIN)/$(ID)/test > $(BIN)/$(ID)/times.txt

build: $(BIN)/$(ID)/test
bench: $(BIN)/$(ID)/times.txt

include ../support/Makefile.inc

all: $(BIN)/test

SEED ?= 1

$(BIN)/random_pipeline.generator: random_pipeline_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/random_pipeline.a: $(BIN)/random_pipeline.generator
	@mkdir -p $(@D)
	$^ -g random_pipeline -o $(BIN) -f random_pipeline target=$(HL_TARGET) seed=$(SEED)

$(BIN)/random_pipeline_auto_schedule.a: $(BIN)/random_pipeline.generator
	@mkdir -p $(@D)
	$^ -g random_pipeline -o $(BIN) -f random_pipeline_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true seed=$(SEED)

$(BIN)/test: test.cpp $(BIN)/random_pipeline.a $(BIN)/random_pipeline_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN)

test: $(BIN)/test
	$(BIN)/test

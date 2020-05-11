include ../support/Makefile.inc

AUTOSCHED_SRC = $(realpath ../autoscheduler)
AUTOSCHED_BIN = $(realpath ../autoscheduler/bin)

.PHONY: build clean test

build: $(BIN)/$(HL_TARGET)/filter

$(GENERATOR_BIN)/mat_mul.generator: mat_mul_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/mat_mul.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -e $(GENERATOR_OUTPUTS) -o $(@D) -f mat_mul target=$* auto_schedule=false

$(BIN)/%/mat_mul_auto_schedule.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -e $(GENERATOR_OUTPUTS) -o $(@D) -f mat_mul_auto_schedule target=$*-no_runtime auto_schedule=true $(HL_CUSTOM_AUTOSCHEDULER)

$(BIN)/%/filter: filter.cpp $(BIN)/%/mat_mul.a $(BIN)/%/mat_mul_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS)

run: $(BIN)/$(HL_TARGET)/filter
	@mkdir -p $(@D)
	$^

clean:
	rm -rf $(BIN)

test: run

autotune: # We assume here that our scripts have already built the autotuning binaries
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/$(HL_TARGET)/mat_mul.generator \
		mat_mul \
		"host" \
		$(HL_WEIGHTS_DIR) \
		$(AUTOSCHED_BIN) \
		$(HALIDE_DISTRIB_PATH) \
		$(HL_SAMPLES)


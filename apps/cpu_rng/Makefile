HL_TARGET ?= host

include ../support/Makefile.inc

# Streams and steps per stream: the output is sized well past the last
# level cache, and the materialized state trajectory is eight times it.
LANES ?= 32
STEPS ?= 4194304
PAR ?= false

$(GENERATOR_BIN)/rng.generator: rng_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/rng_ind.a: $(GENERATOR_BIN)/rng.generator
	@mkdir -p $(@D)
	$< -g rng -f rng_ind -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$*-no_runtime lanes=$(LANES) scan=inductive par=$(PAR)

$(BIN)/%/rng_unf.a: $(GENERATOR_BIN)/rng.generator
	@mkdir -p $(@D)
	$< -g rng -f rng_unf -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$*-no_runtime lanes=$(LANES) scan=unfolded par=$(PAR)

$(BIN)/%/rng_rdom.a: $(GENERATOR_BIN)/rng.generator
	@mkdir -p $(@D)
	$< -g rng -f rng_rdom -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$*-no_runtime lanes=$(LANES) scan=rdom par=$(PAR)

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/rng.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=$*

$(BIN)/%/runner: runner.cpp $(BIN)/%/rng_ind.a $(BIN)/%/rng_unf.a $(BIN)/%/rng_rdom.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -DLANES=$(LANES) -DSTEPS=$(STEPS) \
	    -Wall -O2 $^ -o $@ $(LDFLAGS) -lpthread -ldl

test: $(BIN)/$(HL_TARGET)/runner
	$<

clean:
	rm -rf $(BIN)

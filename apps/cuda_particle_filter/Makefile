HL_TARGET ?= host

PARTICLES ?= 1024
STEPS ?= 1024
BATCH ?= 64
CHAIN ?= 12
SCAN ?= inductive

include ../support/Makefile.inc

CXXFLAGS += -DPARTICLES=$(PARTICLES) -DSTEPS=$(STEPS) -DBATCH=$(BATCH)
SHAPE = particles=$(PARTICLES) steps=$(STEPS) batch=$(BATCH) chain=$(CHAIN) scan=$(SCAN)
TERMINATE_HANDLER = ../../test/common/terminate_handler.cpp

all: $(BIN)/$(HL_TARGET)/runner

$(GENERATOR_BIN)/particle_filter.generator: particle_filter_generator.cpp $(TERMINATE_HANDLER) $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/particle_filter.a: $(GENERATOR_BIN)/particle_filter.generator
	@mkdir -p $(@D)
	$^ -g particle_filter -f particle_filter -e $(GENERATOR_OUTPUTS) -o $(@D) target=$* $(SHAPE)

$(BIN)/%/runner: runner.cpp $(BIN)/%/particle_filter.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) -L $(HALIDE_DISTRIB_PATH)/lib -lHalide -Wl,-rpath,$(HALIDE_DISTRIB_PATH)/lib -ldl -lpthread -lz

test: $(BIN)/$(HL_TARGET)/runner
	$(BIN)/$(HL_TARGET)/runner

clean:
	rm -rf $(BIN) $(GENERATOR_BIN)

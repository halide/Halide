HL_TARGET ?= host

include ../support/Makefile.inc

# The cascade's length, and a signal chosen not to fit in the last level
# cache: channels x samples x 4 bytes.
SECTIONS ?= 8
CHANNELS ?= 32
SAMPLES ?= 8388608
# Whether blocks of channels spread across cores.
PAR ?= false

$(GENERATOR_BIN)/biquads.generator: biquads_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/biquads_ind.a: $(GENERATOR_BIN)/biquads.generator
	@mkdir -p $(@D)
	$< -g biquads -f biquads_ind -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$*-no_runtime sections=$(SECTIONS) channels=$(CHANNELS) scan=inductive par=$(PAR)

$(BIN)/%/biquads_unf.a: $(GENERATOR_BIN)/biquads.generator
	@mkdir -p $(@D)
	$< -g biquads -f biquads_unf -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$*-no_runtime sections=$(SECTIONS) channels=$(CHANNELS) scan=unfolded par=$(PAR)

$(BIN)/%/biquads_rdom.a: $(GENERATOR_BIN)/biquads.generator
	@mkdir -p $(@D)
	$< -g biquads -f biquads_rdom -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$*-no_runtime sections=$(SECTIONS) channels=$(CHANNELS) scan=rdom par=$(PAR)

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/biquads.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=$*

# Optional Intel IPP baseline (pip install ipp-devel ipp-static puts it
# in a venv; point IPP_DIR at that venv).
IPP_DIR ?= $(HOME)/.claude/jobs/6e7f4185/tmp/venv
ifneq (,$(wildcard $(IPP_DIR)/include/ipp/ipps.h))
IPP_FLAGS = -DHAVE_IPP -I$(IPP_DIR)/include/ipp -L$(IPP_DIR)/lib -lipps -lippvm -lippcore -Wl,-rpath,$(IPP_DIR)/lib
endif

# The "Finding Fast Filters" strided cascade baseline (fff_biquads.cpp, the
# library vendored under fff/). It needs clang for the vector extensions.
FFF_CXX ?= clang++-21
# Its best shape per configuration: serially, 32-channel blocks with the
# serial IIR (two vector chains per step); across cores, 16-channel blocks
# with the pairwise IIR, whose shorter chain wins once every core has only
# one block.
FFF_STRIDE ?= $(if $(filter true,$(PAR)),16,32)
FFF_PAIRWISE ?= $(if $(filter true,$(PAR)),1,0)
ifneq (,$(shell which $(FFF_CXX) 2>/dev/null))
FFF_FLAGS = -DHAVE_FFF
FFF_OBJ = $(BIN)/$(HL_TARGET)/fff_biquads.o
endif

$(BIN)/%/fff_biquads.o: fff_biquads.cpp fff/*.h
	@mkdir -p $(@D)
	$(FFF_CXX) -O3 -std=c++20 -mavx512f -mavx512bw -Wall -DSECTIONS=$(SECTIONS) -DFFF_STRIDE=$(FFF_STRIDE) -DFFF_PAIRWISE=$(FFF_PAIRWISE) -c $< -o $@

$(BIN)/%/runner: runner.cpp $(BIN)/%/biquads_ind.a $(BIN)/%/biquads_unf.a $(BIN)/%/biquads_rdom.a $(BIN)/%/runtime.a $(FFF_OBJ)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -DCHANNELS=$(CHANNELS) -DSAMPLES=$(SAMPLES) \
	    -DSECTIONS=$(SECTIONS) -DPARALLEL=$(if $(filter true,$(PAR)),1,0) \
	    -Wall -O2 -fopenmp $^ -o $@ $(IPP_FLAGS) $(FFF_FLAGS) $(LDFLAGS) -lpthread -ldl

test: $(BIN)/$(HL_TARGET)/runner
	$<

clean:
	rm -rf $(BIN)

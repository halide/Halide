HL_TARGET ?= host

include ../support/Makefile.inc

# Pairs of QLENxTLEN alignments per batch.
QLEN ?= 256
TLEN ?= 256
BATCH ?= 1024
PAR ?= false

# 64-bit indexing: required when the direction plane exceeds 2GB
# (the big parallel configs), and - unexpectedly - measured 19 percent
# FASTER for the diff8 inner loop than 32-bit indexing at sse4.1
# (verified with alternating-binary A/B runs; static op mixes are
# near-identical, so the cause is dynamic and unexplained). Always on.
LARGE := -large_buffers

$(GENERATOR_BIN)/align.generator: align_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/align_ind.a: $(GENERATOR_BIN)/align.generator
	@mkdir -p $(@D)
	$< -g align -f align_ind -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$*-no_runtime$(LARGE) scan=inductive par=$(PAR)

$(BIN)/%/align_unf.a: $(GENERATOR_BIN)/align.generator
	@mkdir -p $(@D)
	$< -g align -f align_unf -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$*-no_runtime$(LARGE) scan=unfolded par=$(PAR)

$(BIN)/%/align_rdom.a: $(GENERATOR_BIN)/align.generator
	@mkdir -p $(@D)
	$< -g align -f align_rdom -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$*-no_runtime$(LARGE) scan=rdom par=$(PAR)

$(BIN)/%/align_diff8.a: $(GENERATOR_BIN)/align.generator
	@mkdir -p $(@D)
	$< -g align -f align_diff8 -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$*-no_runtime$(LARGE) scan=diff8 par=$(PAR)

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/align.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=$*$(LARGE)

$(BIN)/%/ksw2_gg.o: ksw2/ksw2_gg.c
	@mkdir -p $(@D)
	$(CC) -O3 -march=native -c $< -o $@

$(BIN)/%/ksw2_gg2_sse.o: ksw2/ksw2_gg2_sse.c
	@mkdir -p $(@D)
	$(CC) -O3 -march=native -c $< -o $@

$(BIN)/%/runner: runner.cpp $(BIN)/%/align_ind.a $(BIN)/%/align_unf.a $(BIN)/%/align_rdom.a $(BIN)/%/align_diff8.a $(BIN)/%/runtime.a $(BIN)/%/ksw2_gg.o $(BIN)/%/ksw2_gg2_sse.o
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -DQLEN=$(QLEN) -DTLEN=$(TLEN) -DBATCH=$(BATCH) -DPARALLEL=$(if $(filter true,$(PAR)),1,0) \
	    -Wall -O3 -march=native $^ -o $@ $(LDFLAGS) -lpthread -ldl

test: $(BIN)/$(HL_TARGET)/runner
	$(if $(INTERLEAVE),INTERLEAVE=$(INTERLEAVE)) $<

# The matched-ISA fight: both sides at ksw2's own 128-bit width, with
# interleaved single-shot medians so they share thermal state.
test_matched:
	@$(MAKE) --no-print-directory HL_TARGET=x86-64-linux-sse41 \
	    QLEN=1024 TLEN=1024 BATCH=128 INTERLEAVE=$(or $(INTERLEAVE),15) test

clean:
	rm -rf $(BIN)

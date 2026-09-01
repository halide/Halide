HL_TARGET ?= host

include ../support/Makefile.inc

# Pairs of QLENxTLEN alignments per batch.
QLEN ?= 256
TLEN ?= 256
BATCH ?= 1024
PAR ?= false

$(GENERATOR_BIN)/align.generator: align_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/align_ind.a: $(GENERATOR_BIN)/align.generator
	@mkdir -p $(@D)
	$< -g align -f align_ind -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$*-no_runtime-large_buffers scan=inductive par=$(PAR)

$(BIN)/%/align_unf.a: $(GENERATOR_BIN)/align.generator
	@mkdir -p $(@D)
	$< -g align -f align_unf -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$*-no_runtime-large_buffers scan=unfolded par=$(PAR)

$(BIN)/%/align_rdom.a: $(GENERATOR_BIN)/align.generator
	@mkdir -p $(@D)
	$< -g align -f align_rdom -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$*-no_runtime-large_buffers scan=rdom par=$(PAR)

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/align.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=$*-large_buffers

$(BIN)/%/ksw2_gg.o: ksw2/ksw2_gg.c
	@mkdir -p $(@D)
	$(CC) -O3 -march=native -c $< -o $@

$(BIN)/%/ksw2_gg2_sse.o: ksw2/ksw2_gg2_sse.c
	@mkdir -p $(@D)
	$(CC) -O3 -march=native -c $< -o $@

$(BIN)/%/runner: runner.cpp $(BIN)/%/align_ind.a $(BIN)/%/align_unf.a $(BIN)/%/align_rdom.a $(BIN)/%/runtime.a $(BIN)/%/ksw2_gg.o $(BIN)/%/ksw2_gg2_sse.o
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -DQLEN=$(QLEN) -DTLEN=$(TLEN) -DBATCH=$(BATCH) -DPARALLEL=$(if $(filter true,$(PAR)),1,0) \
	    -Wall -O3 -march=native $^ -o $@ $(LDFLAGS) -lpthread -ldl

test: $(BIN)/$(HL_TARGET)/runner
	$<

clean:
	rm -rf $(BIN)

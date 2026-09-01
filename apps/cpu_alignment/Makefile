HL_TARGET ?= host

include ../support/Makefile.inc

# Pairs of QLENxTLEN alignments per batch.
QLEN ?= 256
TLEN ?= 256
BATCH ?= 1024
PAR ?= false
STREAM ?= true
PROF :=

# 64-bit indexing: required when the direction plane exceeds 2GB (the
# big parallel configs), and measured 19 percent FASTER for the int8
# inner loop than 32-bit indexing at sse4.1 (alternating-binary A/B;
# cause dynamic, unexplained). Always on.
LARGE := -large_buffers

# Two generators, each with three scan forms. align16 is the int16
# absolute-score table; align8 is the int8 Suzuki-Kasahara difference
# formulation.
GENS := align16 align8
FORMS := inductive unfolded rdom
SHORT_inductive := ind
SHORT_unfolded := unf
SHORT_rdom := rdom

$(GENERATOR_BIN)/%.generator: %_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

# align16_ind.a, align16_unf.a, ..., align8_rdom.a
define GEN_rule
$$(BIN)/%/$(1)_$$(SHORT_$(2)).a: $$(GENERATOR_BIN)/$(1).generator
	@mkdir -p $$(@D)
	$$< -g $(1) -f $(1)_$$(SHORT_$(2)) -e $$(GENERATOR_OUTPUTS) -o $$(@D) \
	    target=$$*-no_runtime$$(LARGE)$$(PROF) scan=$(2) par=$$(PAR) stream=$$(STREAM)
endef
$(foreach g,$(GENS),$(foreach f,$(FORMS),$(eval $(call GEN_rule,$(g),$(f)))))

STATIC_LIBS := $(foreach g,$(GENS),$(foreach f,$(FORMS),$(BIN)/$(HL_TARGET)/$(g)_$(SHORT_$(f)).a))

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/align16.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=$*$(LARGE)$(PROF)

$(BIN)/%/ksw2_gg.o: ksw2/ksw2_gg.c
	@mkdir -p $(@D)
	$(CC) -O3 -march=native -c $< -o $@

$(BIN)/%/ksw2_gg2_sse.o: ksw2/ksw2_gg2_sse.c
	@mkdir -p $(@D)
	$(CC) -O3 -march=native -c $< -o $@

# Optional parasail baseline: build https://github.com/jeffdaily/parasail
# with cmake into $(PARASAIL_DIR)/build and this picks it up.
PARASAIL_DIR ?= $(HOME)/projects/parasail
ifneq (,$(wildcard $(PARASAIL_DIR)/build/libparasail.a))
PARASAIL_FLAGS = -DHAVE_PARASAIL -I$(PARASAIL_DIR) $(PARASAIL_DIR)/build/libparasail.a
endif

$(BIN)/%/runner: runner.cpp $(STATIC_LIBS) $(BIN)/%/runtime.a $(BIN)/%/ksw2_gg.o $(BIN)/%/ksw2_gg2_sse.o
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -DQLEN=$(QLEN) -DTLEN=$(TLEN) -DBATCH=$(BATCH) \
	    -DPARALLEL=$(if $(filter true,$(PAR)),1,0) \
	    -Wall -O3 -march=native $(filter %.cpp %.a %.o,$^) $(PARASAIL_FLAGS) -o $@ $(LDFLAGS) -lpthread -ldl

test: $(BIN)/$(HL_TARGET)/runner
	$(if $(INTERLEAVE),INTERLEAVE=$(INTERLEAVE)) $<

# The matched-ISA fight: both sides at ksw2's own 128-bit width, with
# interleaved single-shot medians so they share thermal state.
test_matched:
	@$(MAKE) --no-print-directory HL_TARGET=x86-64-linux-sse41 \
	    QLEN=1024 TLEN=1024 BATCH=128 INTERLEAVE=$(or $(INTERLEAVE),15) test

clean:
	rm -rf $(BIN)

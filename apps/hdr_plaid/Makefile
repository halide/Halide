include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin

SRCS = hdr_plaid_generator.cpp align.cpp finish.cpp merge.cpp util.cpp

# 'classic' will exceed 2GB in some allocations, so enable -large
HL_TARGET_LARGE=$(HL_TARGET)-large_buffers

$(BIN)/hdr_plaid.generator: $(SRCS) $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/hdr_plaid.a: $(BIN)/hdr_plaid.generator
	@mkdir -p $(@D)
	$< -g hdr_plaid -f hdr_plaid -o $(BIN) target=$(HL_TARGET_LARGE) auto_schedule=false

$(BIN)/hdr_plaid_classic_auto_schedule.a: $(BIN)/hdr_plaid.generator
	@mkdir -p $(@D)
	$< -g hdr_plaid -f hdr_plaid_classic_auto_schedule -o $(BIN) target=$(HL_TARGET_LARGE) auto_schedule=true

$(BIN)/hdr_plaid_auto_schedule.a: $(BIN)/hdr_plaid.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/weights \
	$< -g hdr_plaid -f hdr_plaid_auto_schedule -o $(BIN) target=$(HL_TARGET_LARGE) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so

$(BIN)/process: process.cpp $(BIN)/hdr_plaid.a $(BIN)/hdr_plaid_classic_auto_schedule.a $(BIN)/hdr_plaid_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)

test: $(BIN)/process
	$(BIN)/process

autotune: $(BIN)/hdr_plaid.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/hdr_plaid.generator \
		hdr_plaid \
		x86-64-avx2 \
		$(AUTOSCHED_SRC)/weights \
		$(AUTOSCHED_BIN)

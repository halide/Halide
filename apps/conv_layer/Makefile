include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

BIN ?= bin

all: $(BIN)/process

$(BIN)/conv_layer.generator: conv_layer_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/conv_layer.a: $(BIN)/conv_layer.generator
	@-mkdir -p $(BIN)
	$^ -g conv_layer -o $(BIN) -f conv_layer target=$(HL_TARGET) auto_schedule=false

$(BIN)/conv_layer_classic_auto_schedule.a: $(BIN)/conv_layer.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=24 \
	HL_COMPUTE_ROOT_AND_INLINE_ONLY=1 \
	$< -g conv_layer -f conv_layer_classic_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) -e static_library,h,assembly,stmt 2> stderr_classic.txt > stdout_classic.txt

$(BIN)/conv_layer_auto_schedule.a: $(BIN)/conv_layer.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=24 \
	$< -g conv_layer -f conv_layer_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) 2> stderr.txt > stdout.txt

$(BIN)/process: process.cpp $(BIN)/conv_layer.a $(BIN)/conv_layer_auto_schedule.a $(BIN)/conv_layer_classic_auto_schedule.a
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS)

run: $(BIN)/process
	@-mkdir -p $(BIN)
	$(BIN)/process

clean:
	rm -rf $(BIN)

test: run

autotune: $(BIN)/conv_layer.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/conv_layer.generator \
		conv_layer \
		x86-64-avx2 \
		$(AUTOSCHED_SRC)/weights \
		$(AUTOSCHED_BIN)

include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

all: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/conv_layer.generator: conv_layer_generator.cpp ../autoscheduler/SimpleAutoSchedule.h ../autoscheduler/SimpleAutoSchedule.cpp ../autoscheduler/DerivativeUtils.h ../autoscheduler/DerivativeUtils.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/%/conv_layer.a: $(GENERATOR_BIN)/conv_layer.generator
	@mkdir -p $(@D)
	$^ -g conv_layer -e $(GENERATOR_OUTPUTS) -o $(@D) -f conv_layer target=$* auto_schedule=false

$(BIN)/%/conv_layer_auto_schedule.a: $(GENERATOR_BIN)/conv_layer.generator
	@mkdir -p $(@D)
	$^ -g conv_layer -e $(GENERATOR_OUTPUTS) -o $(@D) -f conv_layer_auto_schedule target=$*-no_runtime auto_schedule=true

$(BIN)/%/conv_layer_classic_auto_schedule.a: $(GENERATOR_BIN)/conv_layer.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	HL_COMPUTE_ROOT_AND_INLINE_ONLY=1 \
	$^ -g conv_layer -f conv_layer_classic_auto_schedule -e $(GENERATOR_OUTPUTS) -o $(@D) target=$* auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) 2> stderr_classic.txt > stdout_classic.txt

$(BIN)/%/conv_layer_auto_schedule.a: $(GENERATOR_BIN)/conv_layer.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$^ -g conv_layer -f conv_layer_auto_schedule -e $(GENERATOR_OUTPUTS) -o $(@D) target=$* auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) 2> stderr.txt > stdout.txt

$(BIN)/%/conv_layer_simple_auto_schedule.a: $(GENERATOR_BIN)/conv_layer.generator
	@mkdir -p $(@D)
	HL_USE_SIMPLE_AUTOSCHEDULER=1 \
	$^ -g conv_layer -f conv_layer_simple_auto_schedule -e $(GENERATOR_OUTPUTS) -o $(@D) target=$*-no_runtime auto_schedule=false

$(BIN)/%/process: process.cpp $(BIN)/%/conv_layer.a $(BIN)/%/conv_layer_auto_schedule.a $(BIN)/%/conv_layer_classic_auto_schedule.a $(BIN)/%/conv_layer_simple_auto_schedule.a
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS)

run: $(BIN)/$(HL_TARGET)/process
	@mkdir -p $(@D)
	$^

clean:
	rm -rf $(BIN)

test: run

autotune: $(GENERATOR_BIN)/conv_layer.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/conv_layer.generator \
		conv_layer \
		$(HL_TARGET) \
		$(AUTOSCHED_SRC)/gpu_weights \
		$(AUTOSCHED_BIN)

include ../support/Makefile.inc
include ../support/autoscheduler.inc


HL_TARGET=host-cuda

all: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/nl_means.generator: nl_means_generator.cpp ../autoscheduler/SimpleAutoSchedule.h ../autoscheduler/SimpleAutoSchedule.cpp ../autoscheduler/DerivativeUtils.h ../autoscheduler/DerivativeUtils.cpp $(GENERATOR_DEPS)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/%/nl_means.a: $(GENERATOR_BIN)/nl_means.generator
	@mkdir -p $(@D)
	$^ -g nl_means -e $(GENERATOR_OUTPUTS) -o $(@D) -f nl_means target=$* auto_schedule=false

$(BIN)/%/nl_means_classic_auto_schedule.a: $(GENERATOR_BIN)/nl_means.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(BIN)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	HL_COMPUTE_ROOT_AND_INLINE_ONLY=1 \
	$^ -g nl_means -e $(GENERATOR_OUTPUTS) -o $(@D) -f nl_means_classic_auto_schedule target=$*-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) 2> stderr_classic.txt > stdout_classic.txt

$(BIN)/%/nl_means_auto_schedule.a: $(GENERATOR_BIN)/nl_means.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(BIN)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$^ -g nl_means -e $(GENERATOR_OUTPUTS) -o $(@D) -f nl_means_auto_schedule target=$*-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) 2> stderr.txt > stdout.txt

$(BIN)/%/nl_means_simple_auto_schedule.a: $(GENERATOR_BIN)/nl_means.generator
	@mkdir -p $(@D)
	HL_USE_SIMPLE_AUTOSCHEDULER=1 \
	$^ -g nl_means -e $(GENERATOR_OUTPUTS) -o $(@D) -f nl_means_simple_auto_schedule target=$*$(HL_TARGET)-no_runtime auto_schedule=false

$(BIN)/%/process: process.cpp $(BIN)/%/nl_means.a $(BIN)/%/nl_means_classic_auto_schedule.a $(BIN)/%/nl_means_auto_schedule.a $(BIN)/%/nl_means_simple_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb.png 7 7 0.12 10 $@

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png

autotune: $(GENERATOR_BIN)/nl_means.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/nl_means.generator \
		nl_means \
		$(HL_TARGET) \
		$(AUTOSCHED_SRC)/gpu_weights \
		$(AUTOSCHED_BIN)

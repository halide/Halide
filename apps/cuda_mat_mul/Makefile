include ../support/Makefile.inc
include ../support/autoscheduler.inc

MATRIX_SIZE ?= 1024

CUDA_SDK ?= /usr/local/cuda-8.0

CXXFLAGS += -I $(CUDA_SDK)/include -I../support/
LDFLAGS += -L $(CUDA_SDK)/lib64

HL_TARGET = host-cuda-cuda_capability_50

GRADIENT_AUTOSCHED_BIN=../gradient_autoscheduler/bin

all: $(BIN)/$(HL_TARGET)/runner

$(GENERATOR_BIN)/mat_mul.generator: mat_mul_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/mat_mul.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -e $(GENERATOR_OUTPUTS) -o $(@D) target=$(HL_TARGET) size=$(MATRIX_SIZE)

$(BIN)/%/mat_mul_gradient_auto_schedule.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -e $(GENERATOR_OUTPUTS) -o $(@D) -f mat_mul_gradient_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true -p $(GRADIENT_AUTOSCHED_BIN)/libgradient_autoscheduler.so -s Li2018

$(BIN)/%/runner: runner.cpp $(BIN)/%/mat_mul.a $(BIN)/%/mat_mul_gradient_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) -lcudart -lcublas

test: $(BIN)/$(HL_TARGET)/runner
	HL_CUDA_JIT_MAX_REGISTERS=256 $^ $(MATRIX_SIZE)

clean:
	rm -rf $(BIN)

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
SAMPLES_DIR ?= autotuned_samples
autotune: $(GENERATOR_BIN)/mat_mul.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/retrain_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_TARGET=$(HL_TARGET) \
	HL_MACHINE_PARAMS=80,1,1 \
	SAMPLES_DIR=$(SAMPLES_DIR) \
	HL_DEBUG_CODEGEN=0 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/mat_mul.generator \
		mat_mul \
		$(HL_TARGET) \
		$(AUTOSCHED_SRC)/gpu.weights \
		$(AUTOSCHED_BIN) \
		$(BATCH_ID) \
		$(TRAIN_ONLY)

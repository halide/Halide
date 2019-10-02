include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin
MATRIX_SIZE ?= 1024

CUDA_SDK ?= /usr/local/cuda-8.0

CXXFLAGS += -I $(CUDA_SDK)/include
LDFLAGS += -L $(CUDA_SDK)/lib64

all: $(BIN)/runner

$(BIN)/mat_mul.generator: mat_mul_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/mat_mul.a: $(BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -o $(BIN) target=host-cuda-cuda_capability_50 size=$(MATRIX_SIZE)

$(BIN)/runner: runner.cpp $(BIN)/mat_mul.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) -lcudart -lcublas

test: $(BIN)/runner
	HL_CUDA_JIT_MAX_REGISTERS=256 $(BIN)/runner $(MATRIX_SIZE)

clean:
	rm -rf $(BIN)

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
SAMPLES_DIR ?= autotuned_samples
autotune: $(BIN)/mat_mul.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_TARGET=host-cuda \
	HL_MACHINE_PARAMS=80,1,1 \
	SAMPLES_DIR=$(SAMPLES_DIR) \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=0 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/mat_mul.generator \
		mat_mul \
		host-cuda \
		$(AUTOSCHED_SRC)/gpu_weights \
		$(AUTOSCHED_BIN) \
		$(BATCH_ID) \
		$(TRAIN_ONLY)

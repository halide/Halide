include ../support/Makefile.inc

MATRIX_SIZE ?= 1024

CUDA_SDK ?= /usr/local/cuda

CXXFLAGS += -I $(CUDA_SDK)/include
LDFLAGS += -L $(CUDA_SDK)/lib64 -Wl,-rpath,$(CUDA_SDK)/lib64

all: $(BIN)/$(HL_TARGET)/runner

$(GENERATOR_BIN)/mat_mul.generator: mat_mul_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/mat_mul.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -e $(GENERATOR_OUTPUTS) -o $(@D) target=host-cuda-cuda_capability_50 size=$(MATRIX_SIZE)

# Tile-IR variant: f16 inputs, f32 acc/output, sm_120 + cuda_tile_ir target.
# Function name is mat_mul_tileir to avoid colliding with the f32 one.
$(BIN)/%/mat_mul_tileir.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -f mat_mul_tileir -e $(GENERATOR_OUTPUTS) -o $(@D) target=host-cuda-cuda_tile_ir-cuda_capability_50 size=$(MATRIX_SIZE) tile_ir=true

$(BIN)/%/runner: runner.cpp $(BIN)/%/mat_mul.a $(BIN)/%/mat_mul_tileir.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) -lcudart -lcublas

test: $(BIN)/$(HL_TARGET)/runner
	HL_CUDA_JIT_MAX_REGISTERS=256 $^ $(MATRIX_SIZE)

clean:
	rm -rf $(BIN)

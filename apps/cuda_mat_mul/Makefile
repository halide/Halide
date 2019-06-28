include ../support/Makefile.inc

MATRIX_SIZE ?= 1024

CUDA_SDK ?= /usr/local/cuda-8.0

CXXFLAGS += -I $(CUDA_SDK)/include
LDFLAGS += -L $(CUDA_SDK)/lib64

all: $(BIN)/$(HL_TARGET)/runner

$(GENERATOR_BIN)/mat_mul.generator: mat_mul_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/mat_mul.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -e $(GENERATOR_OUTPUTS) -o $(@D) target=host-cuda-cuda_capability_50 size=$(MATRIX_SIZE)

$(BIN)/%/runner: runner.cpp $(BIN)/%/mat_mul.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) -lcudart -lcublas

test: $(BIN)/$(HL_TARGET)/runner
	HL_CUDA_JIT_MAX_REGISTERS=256 $^ $(MATRIX_SIZE)

clean:
	rm -rf $(BIN)

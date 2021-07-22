include ../support/Makefile.inc

MATRIX_SIZE ?= 2048

CUDA_SDK ?= /usr/local/cuda-10.0

CXXFLAGS += -I $(CUDA_SDK)/include
LDFLAGS += -L $(CUDA_SDK)/lib64 -Wl,-rpath,$(CUDA_SDK)/lib64

all: $(BIN)/$(HL_TARGET)/runner

$(GENERATOR_BIN)/mat_mul.generator: mat_mul_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/mat_mul_50.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul_50 -e $(GENERATOR_OUTPUTS) -o $(@D) target=host-cuda-cuda_capability_50 size=$(MATRIX_SIZE)

$(BIN)/%/mat_mul_70.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul_70 -e $(GENERATOR_OUTPUTS) -o $(@D) target=host-cuda-cuda_capability_50 size=$(MATRIX_SIZE)

$(BIN)/%/runner: runner.cpp $(BIN)/%/mat_mul_50.a $(BIN)/%/mat_mul_70.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) -lcudart -lcublas

test: $(BIN)/$(HL_TARGET)/runner
	HL_CUDA_JIT_MAX_REGISTERS=256 $^ $(MATRIX_SIZE)

clean:
	rm -rf $(BIN)

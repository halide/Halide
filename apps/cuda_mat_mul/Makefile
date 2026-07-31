include ../support/Makefile.inc

MATRIX_SIZE ?= 1024

CUDA_SDK ?= /usr/local/cuda

CXXFLAGS += -I $(CUDA_SDK)/include
LDFLAGS += -L $(CUDA_SDK)/lib64 -Wl,-rpath,$(CUDA_SDK)/lib64

# The float variant runs anywhere, but the half variant is scheduled onto the
# tensor cores, which need compute capability 7.0 or above.
FLOAT_TARGET ?= host-cuda-cuda_capability_50
HALF_TARGET ?= host-cuda-cuda_capability_80

all: $(BIN)/$(HL_TARGET)/runner

$(GENERATOR_BIN)/mat_mul.generator: mat_mul_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/mat_mul.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -f mat_mul -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(FLOAT_TARGET) size=$(MATRIX_SIZE) A.type=float32 B.type=float32

$(BIN)/%/mat_mul_f16.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -f mat_mul_f16 -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(HALF_TARGET) size=$(MATRIX_SIZE) A.type=float16 B.type=float16

$(BIN)/%/runner: runner.cpp $(BIN)/%/mat_mul.a $(BIN)/%/mat_mul_f16.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) -lcudart -lcublas

test: $(BIN)/$(HL_TARGET)/runner
	$^ $(MATRIX_SIZE)

clean:
	rm -rf $(BIN)

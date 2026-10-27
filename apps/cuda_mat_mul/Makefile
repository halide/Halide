include ../support/Makefile.inc

MATRIX_SIZE ?= 1024

CUDA_TARGET ?= host-cuda-cuda_capability_80

CUDA_SDK ?= /usr/local/cuda

CXXFLAGS += -I $(CUDA_SDK)/include
LDFLAGS += -L $(CUDA_SDK)/lib64 -Wl,-rpath,$(CUDA_SDK)/lib64

all: $(BIN)/$(HL_TARGET)/runner

$(GENERATOR_BIN)/mat_mul.generator: mat_mul_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/mat_mul.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -e $(GENERATOR_OUTPUTS) -o $(@D) target=$(CUDA_TARGET) size=$(MATRIX_SIZE)

$(BIN)/%/runner: runner.cpp $(BIN)/%/mat_mul.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) -lcudart -lcublas

test: $(BIN)/$(HL_TARGET)/runner
	$^ $(MATRIX_SIZE)

clean:
	rm -rf $(BIN)

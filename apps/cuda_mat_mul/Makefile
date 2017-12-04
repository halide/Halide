include ../support/Makefile.inc

BIN ?= bin
MATRIX_SIZE ?= 1024

CXXFLAGS += -I /usr/local/cuda-8.0/include
LDFLAGS += -L /usr/local/cuda-8.0/lib64

all: $(BIN)/runner

$(BIN)/mat_mul.generator: mat_mul_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

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

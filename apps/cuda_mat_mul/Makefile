include ../support/Makefile.inc

BIN ?= bin
MATRIX_SIZE ?= 1024

all: $(BIN)/runner

$(BIN)/mat_mul_exec: mat_mul_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/mat_mul.a: $(BIN)/mat_mul_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN) target=host-cuda-cuda_capability_50 size=$(MATRIX_SIZE)

$(BIN)/runner: runner.cpp $(BIN)/mat_mul.a
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) -lcudart -lcublas

test: $(BIN)/runner
	HL_CUDA_JIT_MAX_REGISTERS=256 $(BIN)/runner $(MATRIX_SIZE)

clean:
	rm -rf $(BIN)

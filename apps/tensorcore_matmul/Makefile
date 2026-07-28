include ../support/Makefile.inc

MATMUL_M ?= 1024
MATMUL_N ?= 1024
MATMUL_K ?= 1024

# The wmma instructions require compute capability 7.0 or above.
TENSORCORE_TARGET ?= host-cuda-cuda_capability_80

all: $(BIN)/$(HL_TARGET)/runner

$(GENERATOR_BIN)/matmul.generator: matmul_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/matmul_cudaonly.a: $(GENERATOR_BIN)/matmul.generator
	@mkdir -p $(@D)
	$^ -g matmul -f matmul_cudaonly -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(TENSORCORE_TARGET) gpu_schedule=cudaonly \
	    M=$(MATMUL_M) N=$(MATMUL_N) K=$(MATMUL_K)

$(BIN)/%/matmul_tensorcore.a: $(GENERATOR_BIN)/matmul.generator
	@mkdir -p $(@D)
	$^ -g matmul -f matmul_tensorcore -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(TENSORCORE_TARGET) gpu_schedule=tensorcore \
	    M=$(MATMUL_M) N=$(MATMUL_N) K=$(MATMUL_K)

$(BIN)/%/runner: runner.cpp $(BIN)/%/matmul_cudaonly.a $(BIN)/%/matmul_tensorcore.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall \
	    -DMATMUL_M=$(MATMUL_M) -DMATMUL_N=$(MATMUL_N) -DMATMUL_K=$(MATMUL_K) \
	    $^ -o $@ $(LDFLAGS) $(LIBHALIDE_LDFLAGS)

test: $(BIN)/$(HL_TARGET)/runner
	$^

clean:
	rm -rf $(BIN)

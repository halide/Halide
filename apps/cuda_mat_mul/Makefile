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
	$^ -g mat_mul -f mat_mul -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(CUDA_TARGET) size=$(MATRIX_SIZE) \
	    A.type=float32 B.type=float32 out.type=float32

$(BIN)/%/mat_mul_f16.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -f mat_mul_f16 -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(CUDA_TARGET) size=$(MATRIX_SIZE) \
	    A.type=float16 B.type=float16 out.type=float32

$(BIN)/%/mat_mul_f16_acc16.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -f mat_mul_f16_acc16 -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(CUDA_TARGET) size=$(MATRIX_SIZE) \
	    A.type=float16 B.type=float16 out.type=float16

$(BIN)/%/mat_mul_bf16.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -f mat_mul_bf16 -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(CUDA_TARGET) size=$(MATRIX_SIZE) \
	    A.type=bfloat16 B.type=bfloat16 out.type=float32

$(BIN)/%/mat_mul_u8.a: $(GENERATOR_BIN)/mat_mul.generator
	@mkdir -p $(@D)
	$^ -g mat_mul -f mat_mul_u8 -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(CUDA_TARGET) size=$(MATRIX_SIZE) \
	    A.type=uint8 B.type=uint8 out.type=int32

$(BIN)/%/runner: runner.cpp $(BIN)/%/mat_mul.a $(BIN)/%/mat_mul_f16.a \
                 $(BIN)/%/mat_mul_bf16.a $(BIN)/%/mat_mul_u8.a \
                 $(BIN)/%/mat_mul_f16_acc16.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) $(LIBHALIDE_LDFLAGS) -lcudart -lcublas

test: $(BIN)/$(HL_TARGET)/runner
	$^ $(MATRIX_SIZE)

clean:
	rm -rf $(BIN)

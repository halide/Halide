include ../support/Makefile.inc

QUERIES ?= 16384
KEYS ?= 64
DEPTH ?= 64
OUT_DEPTH ?= 64

CUDA_SDK ?= /usr/local/cuda

CXXFLAGS += -I $(CUDA_SDK)/include
LDFLAGS += -L $(CUDA_SDK)/lib64 -Wl,-rpath,$(CUDA_SDK)/lib64

# The schedule is scheduled onto the tensor cores, which need compute
# capability 7.0 or above.
TARGET ?= host-cuda-cuda_capability_80

SHAPE = queries=$(QUERIES) keys=$(KEYS) depth=$(DEPTH) out_depth=$(OUT_DEPTH)
CXXFLAGS += -DQUERIES=$(QUERIES) -DKEYS=$(KEYS) -DDEPTH=$(DEPTH) -DOUT_DEPTH=$(OUT_DEPTH)

all: $(BIN)/$(HL_TARGET)/runner

$(GENERATOR_BIN)/attention.generator: attention_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/attention.a: $(GENERATOR_BIN)/attention.generator
	@mkdir -p $(@D)
	$^ -g attention -f attention -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(TARGET) $(SHAPE)

$(BIN)/%/runner: runner.cpp $(BIN)/%/attention.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) $(LIBHALIDE_LDFLAGS) -lcudart -lcublas

test: $(BIN)/$(HL_TARGET)/runner
	$^

clean:
	rm -rf $(BIN)

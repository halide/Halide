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

TILES_Y ?= 0
WARPS ?= 0
STAGE ?= true
PAD ?= 0

# The flash schedule walks the keys rather than holding them all, so it has its
# own shape knobs.
FLASH_TILES_Y ?= 0
FLASH_WARPS ?= 0
FLASH_STAGE ?= true
FLASH_CHUNK ?= 0

SHAPE = queries=$(QUERIES) keys=$(KEYS) depth=$(DEPTH) out_depth=$(OUT_DEPTH)
SHAPE_FLASH := $(SHAPE) tiles_y=$(FLASH_TILES_Y) warps=$(FLASH_WARPS) chunk=$(FLASH_CHUNK) stage=$(FLASH_STAGE) pad=$(PAD)
SHAPE += tiles_y=$(TILES_Y) warps=$(WARPS) stage=$(STAGE) pad=$(PAD)
CXXFLAGS += -DQUERIES=$(QUERIES) -DKEYS=$(KEYS) -DDEPTH=$(DEPTH) -DOUT_DEPTH=$(OUT_DEPTH)

all: $(BIN)/$(HL_TARGET)/runner

$(GENERATOR_BIN)/attention.generator: attention_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/attention.a: $(GENERATOR_BIN)/attention.generator
	@mkdir -p $(@D)
	$^ -g attention -f attention -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(TARGET) $(SHAPE)

$(BIN)/%/attention_flash.a: $(GENERATOR_BIN)/attention.generator
	@mkdir -p $(@D)
	$^ -g attention_flash -f attention_flash -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(TARGET) $(SHAPE_FLASH)

$(BIN)/%/attention_softmax.a: $(GENERATOR_BIN)/attention.generator
	@mkdir -p $(@D)
	$^ -g attention_softmax -f attention_softmax -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$(TARGET) queries=$(QUERIES) keys=$(KEYS)

$(BIN)/%/runner: runner.cpp $(BIN)/%/attention.a $(BIN)/%/attention_flash.a $(BIN)/%/attention_softmax.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) $(LIBHALIDE_LDFLAGS) -lcuda -lcudart -lcublas

test: $(BIN)/$(HL_TARGET)/runner
	$^

clean:
	rm -rf $(BIN)

# The generated libraries depend on generator params that make cannot see, so
# keep them around rather than having make delete them as intermediates and
# then judge the runner up to date without them.
.SECONDARY:

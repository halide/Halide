# Everything here is scheduled onto the tensor cores, so this wants a target
# with a GPU rather than the plain host one the include below would otherwise
# default to. No capability is named, so it is detected from the host, which is
# what you want: naming one can only pin it lower than the part actually is.
# Tensor cores need 7.0 or above.
HL_TARGET ?= host-cuda

# On sm_120 ptxas scalarises the f32x2 instructions llvm emits, so the pairs
# buy no arithmetic and the packing around every tensor core instruction is
# left over: the accumulator can no longer be its own destination, and each
# matmul gains an unpack and a repack. Worth 5% here. sm_100 has the paired
# instructions for real and wants them.
export HL_LLVM_ARGS ?= -nvptx-no-f32x2

include ../support/Makefile.inc

QUERIES ?= 65536
KEYS ?= 1024
DEPTH ?= 64
OUT_DEPTH ?= 64

CUDA_SDK ?= /usr/local/cuda

CXXFLAGS += -I $(CUDA_SDK)/include
LDFLAGS += -L $(CUDA_SDK)/lib64 -Wl,-rpath,$(CUDA_SDK)/lib64

TILES_Y ?= 0
WARPS ?= 0
STAGE ?= true
PAD ?= 0

# The flash schedule walks the keys rather than holding them all, so it has its
# own shape knobs.
FLASH_TILES_Y ?= 0
FLASH_WARPS ?= 0
FLASH_STAGE ?= true
FLASH_STAGE_TILE ?= true
FLASH_STAGE_Q ?= true
FLASH_CHUNK ?= 0

SHAPE = queries=$(QUERIES) keys=$(KEYS) depth=$(DEPTH) out_depth=$(OUT_DEPTH)
SHAPE_FLASH := $(SHAPE) tiles_y=$(FLASH_TILES_Y) warps=$(FLASH_WARPS) chunk=$(FLASH_CHUNK) stage=$(FLASH_STAGE) stage_tile=$(FLASH_STAGE_TILE) stage_q=$(FLASH_STAGE_Q) pad=$(PAD)
# The two-pass (RDom-only) flash filter has its own best shape: a
# narrower step, since its first pass is cheaper per key than its second.
RDOM_TILES_Y ?= $(FLASH_TILES_Y)
RDOM_WARPS ?= $(FLASH_WARPS)
RDOM_CHUNK ?= 32
RDOM_MATERIALIZE ?= false
SHAPE_FLASH_RDOM := $(SHAPE) tiles_y=$(RDOM_TILES_Y) warps=$(RDOM_WARPS) chunk=$(RDOM_CHUNK) materialize=$(RDOM_MATERIALIZE) stage=$(FLASH_STAGE) stage_tile=$(FLASH_STAGE_TILE) stage_q=$(FLASH_STAGE_Q) pad=$(PAD)
SHAPE += tiles_y=$(TILES_Y) warps=$(WARPS) stage=$(STAGE) pad=$(PAD)
CXXFLAGS += -DQUERIES=$(QUERIES) -DKEYS=$(KEYS) -DDEPTH=$(DEPTH) -DOUT_DEPTH=$(OUT_DEPTH) -DFLASH_CHUNK=$(FLASH_CHUNK)

all: $(BIN)/$(HL_TARGET)/runner

$(GENERATOR_BIN)/attention.generator: attention_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/attention.a: $(GENERATOR_BIN)/attention.generator
	@mkdir -p $(@D)
	$^ -g attention -f attention -e $(GENERATOR_OUTPUTS),conceptual_stmt_html -o $(@D) \
	    target=$* $(SHAPE)

$(BIN)/%/attention_flash.a: $(GENERATOR_BIN)/attention.generator
	@mkdir -p $(@D)
	$^ -g attention_flash -f attention_flash -e $(GENERATOR_OUTPUTS),conceptual_stmt_html -o $(@D) \
	    target=$* $(SHAPE_FLASH)

$(BIN)/%/attention_flash_rdom.a: $(GENERATOR_BIN)/attention.generator
	@mkdir -p $(@D)
	$^ -g attention_flash_rdom -f attention_flash_rdom -e $(GENERATOR_OUTPUTS),conceptual_stmt_html -o $(@D) \
	    target=$* $(SHAPE_FLASH_RDOM)

$(BIN)/%/attention_softmax.a: $(GENERATOR_BIN)/attention.generator
	@mkdir -p $(@D)
	$^ -g attention_softmax -f attention_softmax -e $(GENERATOR_OUTPUTS) -o $(@D) \
	    target=$* queries=$(QUERIES) keys=$(KEYS)

$(BIN)/%/runner: runner.cpp $(BIN)/%/attention.a $(BIN)/%/attention_flash.a $(BIN)/%/attention_flash_rdom.a $(BIN)/%/attention_softmax.a
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

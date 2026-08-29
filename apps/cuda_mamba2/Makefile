# Mamba-2's SSD recurrence on the tensor cores. Same story as cuda_attention:
# no capability is named so it is detected from the host.
HL_TARGET ?= host-cuda

export HL_LLVM_ARGS ?= -nvptx-no-f32x2

include ../support/Makefile.inc

# One head's worth of state and channels, the chunk the recurrence is blocked
# into, how long a sequence is, and how many (batch * head) sequences there
# are. The last is where all the parallelism comes from: the chunks of one
# sequence have to be walked in order.
SEQ ?= 4096
STATE ?= 64
CHANNELS ?= 64
CHUNK ?= 64
HEADS ?= 128

CUDA_SDK ?= /usr/local/cuda
CXXFLAGS += -I $(CUDA_SDK)/include
LDFLAGS += -L $(CUDA_SDK)/lib64 -Wl,-rpath,$(CUDA_SDK)/lib64

# inductive, rdom_undef, or rdom - see the generator.
SCAN ?= inductive
WMMA ?= false


SHAPE = seq=$(SEQ) state=$(STATE) channels=$(CHANNELS) chunk=$(CHUNK) heads=$(HEADS) scan=$(SCAN) wmma=$(WMMA)
CXXFLAGS += -DSEQ=$(SEQ) -DSTATE=$(STATE) -DCHANNELS=$(CHANNELS) -DCHUNK=$(CHUNK) -DHEADS=$(HEADS)

all: $(BIN)/$(HL_TARGET)/runner

# generate_filter_main catches and reports what a generator raises, but only
# when libHalide was built with WITH_EXCEPTIONS - the catch is compiled out
# otherwise, while the throw is not. The terminate handler prints the message
# either way, which is what the tests link it for.
TERMINATE_HANDLER = ../../test/common/terminate_handler.cpp

$(GENERATOR_BIN)/mamba2.generator: mamba2_generator.cpp $(TERMINATE_HANDLER) $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/mamba2.a: $(GENERATOR_BIN)/mamba2.generator
	@mkdir -p $(@D)
	$^ -g mamba2 -f mamba2 -e $(GENERATOR_OUTPUTS),conceptual_stmt_html -o $(@D) \
	    target=$* $(SHAPE)

$(BIN)/%/runner: runner.cpp $(BIN)/%/mamba2.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) -L $(HALIDE_DISTRIB_PATH)/lib -lHalide -Wl,-rpath,$(HALIDE_DISTRIB_PATH)/lib -lcuda -lcudart -ldl -lpthread -lz

test: $(BIN)/$(HL_TARGET)/runner
	$^

clean:
	rm -rf $(BIN)

.SECONDARY:

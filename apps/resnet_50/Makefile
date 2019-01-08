include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin
HL_TARGET ?= host
RESNET_BLOCKS ?= $(foreach block,0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15,$(BIN)/resnet50block$(block).a)

all: $(BIN)/process

$(BIN)/resnet50block.generator: Resnet50BlockGenerator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# To use the autoscheduler, set a few environment variables and use the -p flag to the generator to load the autoscheduler as a plugin
$(BIN)/resnet50block%.a: $(BIN)/resnet50block.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_MACHINE_PARAMS=1,1,1 HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=../autoscheduler/weights \
	$(BIN)/resnet50block.generator -g resnet50block -o $(BIN) -f resnet50block$* target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so block_id=$*

$(BIN)/pytorch_weights/ok:
	@mkdir -p $(@D)
	python3 load_weights.py $(@D)
	echo "ok" > $@

$(BIN)/process: process.cpp $(RESNET_BLOCKS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -I $(BIN) -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

test: $(BIN)/process $(BIN)/pytorch_weights/ok
	$< 10 $(BIN)/pytorch_weights cropped_panda.jpg $(BIN)/res50gen_output.bin

clean:
	rm -rf $(BIN)

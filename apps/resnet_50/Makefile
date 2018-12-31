include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin
HL_TARGET ?= host
BLOCK_ID ?=0
MACRO_BLOCK_ID ?= 0
all: $(BIN)/process

$(BIN)/libauto_schedule.so:
	@mkdir -p $(@D)
	@make -C ../autoscheduler ../autoscheduler/$(BIN)/libauto_schedule.so
	@cp ../autoscheduler/bin/libauto_schedule.so $(BIN)

$(BIN)/resnet50block.generator: Resnet50BlockGenerator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# To use the autoscheduler, set a few environment variables and use the -p flag to the generator to load the autoscheduler as a plugin
$(BIN)/block$(BLOCK_ID)/resnet50block.a: $(BIN)/resnet50block.generator $(BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_MACHINE_PARAMS=1,1,1 HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=../autoscheduler/weights \
	$(BIN)/resnet50block.generator -g resnet50block -o $(BIN)/block$(BLOCK_ID) -f resnet50block target=$(HL_TARGET)-no_asserts auto_schedule=true -p bin/libauto_schedule.so block_id=$(BLOCK_ID) macro_block_id=$(MACRO_BLOCK_ID)

$(BIN)/process: process.cpp $(BIN)/resnet50block.a 
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

block: $(BIN)/block$(BLOCK_ID)/resnet50block.a $(BIN)/libautoschedule.so
	@mkdir -p $(@D)

clean:
	rm -rf $(BIN)


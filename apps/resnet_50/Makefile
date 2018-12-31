include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin
HL_TARGET ?= host
BLOCK_ID ?=0
MACRO_BLOCK_ID ?= 0
RESNET_DEPS ?= $(BIN)/block0/resnet50block0.a $(BIN)/block1/resnet50block1.a $(BIN)/block2/resnet50block2.a $(BIN)/block3/resnet50block3.a $(BIN)/block4/resnet50block4.a $(BIN)/block5/resnet50block5.a $(BIN)/block6/resnet50block6.a $(BIN)/block7/resnet50block7.a $(BIN)/block8/resnet50block8.a $(BIN)/block9/resnet50block9.a $(BIN)/block10/resnet50block10.a $(BIN)/block11/resnet50block11.a $(BIN)/block12/resnet50block12.a $(BIN)/block13/resnet50block13.a $(BIN)/block14/resnet50block14.a $(BIN)/block15/resnet50block15.a
RESNET_INC ?= -I$(BIN)/block0 -I$(BIN)/block1 -I$(BIN)/block2 -I$(BIN)/block3 -I$(BIN)/block4 -I$(BIN)/block5 -I$(BIN)/block6 -I$(BIN)/block7 -I$(BIN)/block8 -I$(BIN)/block9 -I$(BIN)/block10 -I$(BIN)/block11 -I$(BIN)/block12 -I$(BIN)/block13 -I$(BIN)/block14 -I$(BIN)/block15

all: $(BIN)/process

$(BIN)/libauto_schedule.so:
	@mkdir -p $(@D)
	@make -C ../autoscheduler ../autoscheduler/bin/libauto_schedule.so
	@cp ../autoscheduler/bin/libauto_schedule.so $(BIN)

$(BIN)/resnet50block.generator: Resnet50BlockGenerator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# To use the autoscheduler, set a few environment variables and use the -p flag to the generator to load the autoscheduler as a plugin
$(BIN)/block$(BLOCK_ID)/resnet50block.a: $(BIN)/resnet50block.generator $(BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_MACHINE_PARAMS=1,1,1 HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=../autoscheduler/weights \
	$(BIN)/resnet50block.generator -g resnet50block -o $(BIN)/block$(BLOCK_ID) -f resnet50block$(BLOCK_ID) target=$(HL_TARGET)-no_asserts auto_schedule=true -p bin/libauto_schedule.so block_id=$(BLOCK_ID) macro_block_id=$(MACRO_BLOCK_ID)

$(BIN)/process: process.cpp $(RESNET_DEPS) 
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(RESNET_INC) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

block: $(BIN)/block$(BLOCK_ID)/resnet50block.a $(BIN)/libauto_schedule.so
	@mkdir -p $(@D)

clean:
	rm -rf $(BIN)


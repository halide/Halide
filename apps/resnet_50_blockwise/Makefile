include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin

HL_TARGET=host-cuda

all: $(BIN)/process

$(BIN)/resnet50block.generator: Resnet50BlockGenerator.cpp ../autoscheduler/SimpleAutoSchedule.h ../autoscheduler/SimpleAutoSchedule.cpp ../autoscheduler/DerivativeUtils.h ../autoscheduler/DerivativeUtils.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/resnet50block_manual%.a: $(BIN)/resnet50block.generator
	@mkdir -p $(@D)
	$(BIN)/resnet50block.generator -g resnet50block -o $(BIN) -e static_library,h,stmt -f resnet50block_manual$* target=$(HL_TARGET) auto_schedule=false block_id=$*

$(BIN)/resnet50block_classic_auto_schedule%.a: $(BIN)/resnet50block.generator
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	HL_COMPUTE_ROOT_AND_INLINE_ONLY=1 \
	$(BIN)/resnet50block.generator -g resnet50block -o $(BIN) -f resnet50block_classic_auto_schedule$* target=$(HL_TARGET) auto_schedule=true classic_auto_schedule_estimates=true block_id=$* -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) -e static_library,h,assembly,stmt 2> stderr_classic.txt > stdout_classic.txt

# To use the autoscheduler, set a few environment variables and use the -p flag to the generator to load the autoscheduler as a plugin
$(BIN)/resnet50block_auto_schedule%.a: $(BIN)/resnet50block.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$(BIN)/resnet50block.generator -g resnet50block -o $(BIN) -f resnet50block_auto_schedule$* target=$(HL_TARGET) auto_schedule=true block_id=$* -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) 2> stderr.txt > stdout.txt

$(BIN)/resnet50block_simple_auto_schedule%.a: $(BIN)/resnet50block.generator
	@mkdir -p $(@D)
	HL_USE_SIMPLE_AUTOSCHEDULER=1 \
	$< -g resnet50block -o $(BIN) -f resnet50block_simple_auto_schedule$* target=$(HL_TARGET)-no_runtime auto_schedule=false -e static_library,h

$(BIN)/pytorch_weights/ok:
	@mkdir -p $(@D)
	python3 load_weights.py $(@D)
	echo "ok" > $@

RESNET_BLOCK_LIBS_MANUAL ?= $(foreach block,0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15,$(BIN)/resnet50block_manual$(block).a)
RESNET_BLOCK_LIBS_CLASSIC_AUTO_SCHEDULE ?= $(foreach block,0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15,$(BIN)/resnet50block_classic_auto_schedule$(block).a)
RESNET_BLOCK_LIBS_AUTO_SCHEDULE ?= $(foreach block,0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15,$(BIN)/resnet50block_auto_schedule$(block).a)
RESNET_BLOCK_LIBS_SIMPLE_AUTO_SCHEDULE ?= $(foreach block,0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15,$(BIN)/resnet50block_simple_auto_schedule$(block).a)

$(BIN)/process: process.cpp $(RESNET_BLOCK_LIBS_MANUAL) $(RESNET_BLOCK_LIBS_CLASSIC_AUTO_SCHEDULE) $(RESNET_BLOCK_LIBS_AUTO_SCHEDULE) $(RESNET_BLOCK_LIBS_SIMPLE_AUTO_SCHEDULE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -I $(BIN) -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

# test_manual, test_auto_schedule, test_classic_auto_schedule,
# test_simple_auto_schedule
test_%: $(BIN)/process $(BIN)/pytorch_weights/ok
	$< 10 $* $(BIN)/pytorch_weights cropped_panda.jpg $(BIN)/res50gen_output.bin

test: test_manual test_classic_auto_schedule test_auto_schedule test_simple_auto_schedule

clean:
	rm -rf $(BIN)

# These are all similar enough that we can just use the same weights, and rotate
# between different variants for each tuning batch. The autotune_loop.sh script will
# add these as extra generator arguments in a round-robin fashion if they are
# supplied as the sixth argument.
AUTOTUNE_ARG_SETS ?= $(foreach block,0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15,block_id=$(block))

autotune: $(BIN)/resnet50block.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/resnet50block.generator \
		resnet50block \
		$(HL_TARGET) \
		$(AUTOSCHED_SRC)/gpu_weights \
		$(AUTOSCHED_BIN) \
		"${AUTOTUNE_ARG_SETS}"

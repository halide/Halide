include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin

HL_TARGET=host-cuda

all: $(BIN)/process

$(BIN)/lens_blur.generator: lens_blur_generator.cpp ../autoscheduler/SimpleAutoSchedule.h ../autoscheduler/SimpleAutoSchedule.cpp ../autoscheduler/DerivativeUtils.h ../autoscheduler/DerivativeUtils.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/lens_blur.a: $(BIN)/lens_blur.generator
	@-mkdir -p $(BIN)
	$< -g lens_blur -o $(BIN) -f lens_blur target=$(HL_TARGET)-large_buffers auto_schedule=false

$(BIN)/lens_blur_classic_auto_schedule.a: $(BIN)/lens_blur.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@-mkdir -p $(BIN)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=2 \
	HL_COMPUTE_ROOT_AND_INLINE_ONLY=1 \
	$< -g lens_blur -o $(BIN) -f lens_blur_classic_auto_schedule target=$(HL_TARGET)-large_buffers-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) -e static_library,h,assembly,stmt 2> stderr_classic.txt > stdout_classic.txt

$(BIN)/lens_blur_auto_schedule.a: $(BIN)/lens_blur.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@-mkdir -p $(BIN)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=2 \
	$< -g lens_blur -o $(BIN) -f lens_blur_auto_schedule target=$(HL_TARGET)-large_buffers-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) -e static_library,h,assembly,stmt 2> stderr.txt > stdout.txt

$(BIN)/lens_blur_simple_auto_schedule.a: $(BIN)/lens_blur.generator
	@mkdir -p $(@D)
	HL_USE_SIMPLE_AUTOSCHEDULER=1 \
	$< -g lens_blur -o $(BIN) -f lens_blur_simple_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=false -e static_library,h

$(BIN)/process: process.cpp $(BIN)/lens_blur.a $(BIN)/lens_blur_classic_auto_schedule.a $(BIN)/lens_blur_auto_schedule.a $(BIN)/lens_blur_simple_auto_schedule.a
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/out.png: $(BIN)/process
	@-mkdir -p $(BIN)
	$(BIN)/process $(IMAGES)/rgb.png 32 13 0.5 32 3 $(BIN)/out.png

clean:
	rm -rf $(BIN)

test: $(BIN)/out.png

autotune: $(BIN)/lens_blur.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=2 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/lens_blur.generator \
		lens_blur \
		$(HL_TARGET)-large_buffers \
		$(AUTOSCHED_SRC)/gpu_weights \
		$(AUTOSCHED_BIN)

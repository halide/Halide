include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin

SRCS = burst_camera_pipe_generator.cpp align.cpp finish.cpp merge.cpp util.cpp

# 'classic' will exceed 2GB in some allocations, so enable -large
HL_TARGET_LARGE=$(HL_TARGET)-large_buffers

$(BIN)/burst_camera_pipe.generator: $(SRCS) $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/burst_camera_pipe.a: $(BIN)/burst_camera_pipe.generator
	@mkdir -p $(@D)
	$< -g burst_camera_pipe -f burst_camera_pipe -o $(BIN) target=$(HL_TARGET_LARGE) auto_schedule=false

$(BIN)/burst_camera_pipe_classic_auto_schedule.a: $(BIN)/burst_camera_pipe.generator
	@mkdir -p $(@D)
	$< -g burst_camera_pipe -f burst_camera_pipe_classic_auto_schedule -o $(BIN) target=$(HL_TARGET_LARGE) auto_schedule=true machine_params=$(HL_MACHINE_PARAMS)

$(BIN)/burst_camera_pipe_auto_schedule.a: $(BIN)/burst_camera_pipe.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/weights \
	$< -g burst_camera_pipe -f burst_camera_pipe_auto_schedule -o $(BIN) target=$(HL_TARGET_LARGE) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so

$(BIN)/process: process.cpp $(BIN)/burst_camera_pipe.a $(BIN)/burst_camera_pipe_classic_auto_schedule.a $(BIN)/burst_camera_pipe_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)

test: $(BIN)/process
	$(BIN)/process

autotune: $(BIN)/burst_camera_pipe.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/retrain_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/burst_camera_pipe.generator \
		burst_camera_pipe \
		x86-64-avx2 \
		$(AUTOSCHED_SRC)/weights \
		$(AUTOSCHED_BIN)

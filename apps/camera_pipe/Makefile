include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

all: $(BIN)/$(HL_TARGET)/process

TIMING_ITERATIONS ?= 5

$(GENERATOR_BIN)/camera_pipe.generator: camera_pipe_generator.cpp ../autoscheduler/SimpleAutoSchedule.h ../autoscheduler/SimpleAutoSchedule.cpp ../autoscheduler/DerivativeUtils.h ../autoscheduler/DerivativeUtils.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/%/camera_pipe.a: $(GENERATOR_BIN)/camera_pipe.generator
	@mkdir -p $(@D)
	$^ -g camera_pipe -e $(GENERATOR_OUTPUTS) -o $(@D) -f camera_pipe target=$* auto_schedule=false

$(BIN)/%/camera_pipe_classic_auto_schedule.a: $(GENERATOR_BIN)/camera_pipe.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	HL_COMPUTE_ROOT_AND_INLINE_ONLY=1 \
	$^ -g camera_pipe -e $(GENERATOR_OUTPUTS) -o $(@D) -f camera_pipe_classic_auto_schedule target=$*-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) 2> stderr_classic.txt > stdout_classic.txt

$(BIN)/%/camera_pipe_auto_schedule.a: $(GENERATOR_BIN)/camera_pipe.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$^ -g camera_pipe -e $(GENERATOR_OUTPUTS) -o $(@D) -f camera_pipe_auto_schedule target=$*-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) 2> stderr.txt > stdout.txt

$(BIN)/%/camera_pipe_simple_auto_schedule.a: $(GENERATOR_BIN)/camera_pipe.generator
	@mkdir -p $(@D)
	HL_USE_SIMPLE_AUTOSCHEDULER=1 \
	$^ -g camera_pipe -e $(GENERATOR_OUTPUTS) -o $(@D) -f camera_pipe_simple_auto_schedule target=$*-no_runtime auto_schedule=false

$(BIN)/%/process: process.cpp $(BIN)/%/camera_pipe.a $(BIN)/%/camera_pipe_classic_auto_schedule.a $(BIN)/%/camera_pipe_auto_schedule.a $(BIN)/%/camera_pipe_simple_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Wall -I$(BIN)/$* $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

$(BIN)/%/process_viz: process.cpp $(BIN)/%-trace_all/camera_pipe.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DNO_AUTO_SCHEDULE -Wall -I$(BIN)/$*-trace_all $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$(BIN)/$*/process $(IMAGES)/bayer_raw.png 3700 2.0 50 1.0 $(TIMING_ITERATIONS) $@ $(BIN)/$*/h_auto.png

../../bin/HalideTraceViz: ../../util/HalideTraceViz.cpp
	$(MAKE) -C ../../ bin/HalideTraceViz

$(BIN)/%/camera_pipe.mp4: $(BIN)/%/process_viz viz.sh $(HALIDE_TRACE_VIZ) ../../bin/HalideTraceViz
	HL_AVCONV=$(HL_AVCONV) bash viz.sh $(@D)

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png

viz: $(BIN)/$(HL_TARGET)/camera_pipe.mp4
	$(HL_VIDEOPLAYER) $^

$(BIN)/%/viz_auto.mp4: $(BIN)/%/process_viz ../support/viz_auto.sh ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash ../support/viz_auto.sh "$(BIN)/$*/process_viz $(IMAGES)/bayer_small.png 3700 1.8 50 1 1 $(BIN)/$*/out.png" ../../bin/HalideTraceViz $@

viz_auto: $(BIN)/$(HL_TARGET)/viz_auto.mp4
	$(HL_VIDEOPLAYER) $^

autotune: $(GENERATOR_BIN)/camera_pipe.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/camera_pipe.generator \
		camera_pipe \
		$(HL_TARGET) \
		$(AUTOSCHED_SRC)/gpu_weights \
		$(AUTOSCHED_BIN)

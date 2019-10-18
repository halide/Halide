include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

all: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/local_laplacian.generator: local_laplacian_generator.cpp ../autoscheduler/SimpleAutoSchedule.h ../autoscheduler/SimpleAutoSchedule.cpp ../autoscheduler/DerivativeUtils.h ../autoscheduler/DerivativeUtils.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/%/local_laplacian.a: $(GENERATOR_BIN)/local_laplacian.generator
	@mkdir -p $(@D)
	$^ -g local_laplacian -e $(GENERATOR_OUTPUTS) -o $(@D) -f local_laplacian target=$* auto_schedule=false

$(BIN)/%/local_laplacian_classic_auto_schedule.a: $(GENERATOR_BIN)/local_laplacian.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	HL_COMPUTE_ROOT_AND_INLINE_ONLY=1 \
	$^ -g local_laplacian -o $(BIN) -f local_laplacian_classic_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) 2> stderr_classic.txt > stdout_classic.txt

$(BIN)/%/local_laplacian_auto_schedule.a: $(GENERATOR_BIN)/local_laplacian.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$^ -g local_laplacian -e $(GENERATOR_OUTPUTS) -o $(@D) -f local_laplacian_auto_schedule target=$*-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) 2> stderr.txt > stdout.txt

$(BIN)/%/local_laplacian_simple_auto_schedule.a: $(GENERATOR_BIN)/local_laplacian.generator
	@mkdir -p $(@D)
	HL_USE_SIMPLE_AUTOSCHEDULER=1 \
	$^ -g local_laplacian -e $(GENERATOR_OUTPUTS) -o $(@D) -f local_laplacian_simple_auto_schedule target=$*-no_runtime auto_schedule=false

$(BIN)/%/process: process.cpp $(BIN)/%/local_laplacian.a $(BIN)/%/local_laplacian_classic_auto_schedule.a $(BIN)/%/local_laplacian_auto_schedule.a $(BIN)/%/local_laplacian_simple_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb.png 8 1 1 10 $@

$(BIN)/%/out.tiff: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb.png 8 1 1 10 $@

$(BIN)/%/process_viz: process.cpp $(BIN)/%-trace_all/local_laplacian.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DNO_AUTO_SCHEDULE -I$(BIN)/$*-trace_all -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

../../bin/HalideTraceViz: ../../util/HalideTraceViz.cpp
	$(MAKE) -C ../../ bin/HalideTraceViz

$(BIN)/%/local_laplacian.mp4: $(BIN)/%/process_viz ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash viz.sh $(<D)

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png $(BIN)/$(HL_TARGET)/out.tiff

viz: $(BIN)/$(HL_TARGET)/local_laplacian.mp4
	$(HL_VIDEOPLAYER) $^

$(BIN)/%/viz_auto.mp4: $(BIN)/%/process_viz ../support/viz_auto.sh ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash ../support/viz_auto.sh "$(BIN)/$*/process_viz $(IMAGES)/rgb_small.png 4 1 1 0 $(BIN)/$*/out_small.png" ../../bin/HalideTraceViz $@

viz_auto: $(BIN)/$(HL_TARGET)/viz_auto.mp4
	$(HL_VIDEOPLAYER) $^

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
SAMPLES_DIR ?= autotuned_samples
autotune: $(GENERATOR_BIN)/local_laplacian.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_TARGET=host-cuda \
	HL_MACHINE_PARAMS=80,1,1 \
	SAMPLES_DIR=$(SAMPLES_DIR) \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=0 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/local_laplacian.generator \
		local_laplacian \
		host-cuda \
		$(AUTOSCHED_SRC)/gpu_weights \
		$(AUTOSCHED_BIN) \
		$(BATCH_ID) \
		$(TRAIN_ONLY)

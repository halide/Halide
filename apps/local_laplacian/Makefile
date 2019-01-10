include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin

HL_TARGET=host-cuda

all: $(BIN)/process

$(BIN)/local_laplacian.generator: local_laplacian_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/local_laplacian.a: $(BIN)/local_laplacian.generator
	@mkdir -p $(@D)
	$^ -g local_laplacian -o $(BIN) -f local_laplacian target=$(HL_TARGET) auto_schedule=false

$(BIN)/local_laplacian_classic_auto_schedule.a: $(BIN)/local_laplacian.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	HL_COMPUTE_ROOT_AND_INLINE_ONLY=1 \
	$< -g local_laplacian -o $(BIN) -f local_laplacian_classic_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) -e static_library,h,assembly,stmt 2> stderr_classic.txt > stdout_classic.txt

$(BIN)/local_laplacian_auto_schedule.a: $(BIN)/local_laplacian.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$< -g local_laplacian -o $(BIN) -f local_laplacian_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) -e static_library,h,s,stmt 2> stderr.txt > stdout.txt

$(BIN)/process: process.cpp $(BIN)/local_laplacian.a $(BIN)/local_laplacian_classic_auto_schedule.a $(BIN)/local_laplacian_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/out.png: $(BIN)/process
	@mkdir -p $(@D)
	$(BIN)/process $(IMAGES)/rgb.png 8 1 1 10 $(BIN)/out.png

$(BIN)/out.tiff: $(BIN)/process
	@mkdir -p $(@D)
	$(BIN)/process $(IMAGES)/rgb.png 8 1 1 10 $(BIN)/out.tiff

# Build rules for generating a visualization of the pipeline using HalideTraceViz
$(BIN)/viz/local_laplacian.a: $(BIN)/local_laplacian.generator
	@mkdir -p $(@D)
	$^ -g local_laplacian -o $(BIN)/viz target=$(HL_TARGET)-trace_all pyramid_levels=6

$(BIN)/process_viz: process.cpp $(BIN)/viz/local_laplacian.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DNO_AUTO_SCHEDULE -I$(BIN)/viz -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

../../bin/HalideTraceViz: ../../util/HalideTraceViz.cpp
	$(MAKE) -C ../../ bin/HalideTraceViz

$(BIN)/local_laplacian.mp4: $(BIN)/process_viz ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash viz.sh $(BIN)

clean:
	rm -rf $(BIN)

test: $(BIN)/out.png $(BIN)/out.tiff

viz: $(BIN)/local_laplacian.mp4
	$(HL_VIDEOPLAYER) $^

$(BIN)/viz_auto.mp4: $(BIN)/process_viz ../support/viz_auto.sh ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash ../support/viz_auto.sh "$(BIN)/process_viz $(IMAGES)/rgb_small.png 4 1 1 0 $(BIN)/out_small.png" ../../bin/HalideTraceViz $@

viz_auto: $(BIN)/viz_auto.mp4
	$(HL_VIDEOPLAYER) $^

autotune: $(BIN)/local_laplacian.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/local_laplacian.generator \
		local_laplacian \
		x86-64-avx2 \
		$(AUTOSCHED_SRC)/weights \
		$(AUTOSCHED_BIN)

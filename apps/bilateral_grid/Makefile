include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

CXXFLAGS += -I../support/

GRADIENT_AUTOSCHED_BIN=../gradient_autoscheduler/bin

all: $(BIN)/$(HL_TARGET)/filter

$(GENERATOR_BIN)/bilateral_grid.generator: bilateral_grid_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/bilateral_grid.a: $(GENERATOR_BIN)/bilateral_grid.generator
	@mkdir -p $(@D)
	$< -g bilateral_grid -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilateral_grid target=$(HL_TARGET) auto_schedule=false

$(BIN)/%/bilateral_grid_auto_schedule.a: $(GENERATOR_BIN)/bilateral_grid.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=48 \
	$< -g bilateral_grid -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilateral_grid_auto_schedule target=$*-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so -s Adams2019 machine_params=$(HL_MACHINE_PARAMS) 2> stderr.txt > stdout.txt

$(BIN)/%/bilateral_grid_gradient_auto_schedule.a: $(GENERATOR_BIN)/bilateral_grid.generator
	@mkdir -p $(@D)
	$^ -g bilateral_grid -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilateral_grid_gradient_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true -p $(GRADIENT_AUTOSCHED_BIN)/libgradient_autoscheduler.so -s Li2018

$(BIN)/%/filter: filter.cpp $(BIN)/%/bilateral_grid.a $(BIN)/%/bilateral_grid_auto_schedule.a $(BIN)/%/bilateral_grid_gradient_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

$(BIN)/%/filter_viz: filter.cpp $(BIN)/%-trace_all/bilateral_grid.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DNO_AUTO_SCHEDULE -I$(BIN)/$*-trace_all $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

../../bin/HalideTraceViz: ../../util/HalideTraceViz.cpp
	$(MAKE) -C ../../ bin/HalideTraceViz

$(BIN)/%/bilateral_grid.mp4: $(BIN)/%/filter_viz viz.sh ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash viz.sh $(BIN)/$*

$(BIN)/%/out.png: $(BIN)/%/filter
	@mkdir -p $(@D)
	$< $(IMAGES)/gray.png $@ 0.1 10

$(BIN)/%/out.tiff: $(BIN)/%/filter
	@mkdir -p $(@D)
	$< $(IMAGES)/gray.png $@ 0.1 10

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png $(BIN)/$(HL_TARGET)/out.tiff

viz: $(BIN)/$(HL_TARGET)/bilateral_grid.mp4
	$(HL_VIDEOPLAYER) $^

$(BIN)/%/viz_auto.mp4: $(BIN)/%/filter_viz ../support/viz_auto.sh ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash ../support/viz_auto.sh "$(BIN)/$*/filter_viz $(IMAGES)/gray_small.png $(BIN)/$*/out_small.png 0.2 0" ../../bin/HalideTraceViz $@

viz_auto: $(BIN)/$(HL_TARGET)/viz_auto.mp4
	$(HL_VIDEOPLAYER) $^

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
SAMPLES_DIR ?= autotuned_samples
autotune: $(GENERATOR_BIN)/bilateral_grid.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/retrain_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_TARGET=host-cuda \
	HL_MACHINE_PARAMS=80,1,1 \
	SAMPLES_DIR=$(SAMPLES_DIR) \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu.weights \
	HL_DEBUG_CODEGEN=0 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/bilateral_grid.generator \
		bilateral_grid \
		host-cuda \
		$(AUTOSCHED_SRC)/gpu.weights \
		$(AUTOSCHED_BIN) \
		$(BATCH_ID) \
		$(TRAIN_ONLY)

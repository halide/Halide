include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

all: $(BIN)/filter

$(BIN)/bilateral_grid.generator: bilateral_grid_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/bilateral_grid.a: $(BIN)/bilateral_grid.generator
	@mkdir -p $(@D)
	$< -g bilateral_grid -o $(BIN) -f bilateral_grid target=$(HL_TARGET) auto_schedule=false

$(BIN)/bilateral_grid_classic_auto_schedule.a: $(BIN)/bilateral_grid.generator
	@mkdir -p $(@D)
	$< -g bilateral_grid -o $(BIN) -f bilateral_grid_classic_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true -e static_library,h,schedule

$(BIN)/bilateral_grid_auto_schedule.a: $(BIN)/bilateral_grid.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/weights \
	HL_MACHINE_PARAMS=1024,1,1 \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=8 \
	$< -g bilateral_grid -o $(BIN) -f bilateral_grid_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so -e static_library,h,schedule 2> stderr.txt > stdout.txt

$(BIN)/viz/bilateral_grid.a: $(BIN)/bilateral_grid.generator
	@mkdir -p $(@D)
	$^ -g bilateral_grid -o $(BIN)/viz target=$(HL_TARGET)-trace_all

$(BIN)/filter: filter.cpp $(BIN)/bilateral_grid.a $(BIN)/bilateral_grid_classic_auto_schedule.a $(BIN)/bilateral_grid_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -O3 -ffast-math -Wall -Werror -I$(BIN) $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

$(BIN)/filter_viz: $(BIN)/viz/bilateral_grid.a filter.cpp ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DNO_AUTO_SCHEDULE -O3 -ffast-math -Wall -Werror -I$(BIN)/viz filter.cpp $(BIN)/viz/bilateral_grid.a -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

../../bin/HalideTraceViz: ../../util/HalideTraceViz.cpp
	$(MAKE) -C ../../ bin/HalideTraceViz

$(BIN)/bilateral_grid.mp4: $(BIN)/filter_viz viz.sh ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash viz.sh $(BIN)

$(BIN)/out.png: $(BIN)/filter
	@mkdir -p $(@D)
	$(BIN)/filter $(IMAGES)/gray.png $(BIN)/out.png 0.1 10

$(BIN)/out.tiff: $(BIN)/filter
	@mkdir -p $(@D)
	$(BIN)/filter $(IMAGES)/gray.png $(BIN)/out.tiff 0.1 10

clean:
	rm -rf $(BIN)

test: $(BIN)/out.png $(BIN)/out.tiff

viz: $(BIN)/bilateral_grid.mp4
	$(HL_VIDEOPLAYER) $^

$(BIN)/viz_auto.mp4: $(BIN)/filter_viz ../support/viz_auto.sh ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash ../support/viz_auto.sh "$(BIN)/filter_viz $(IMAGES)/gray_small.png $(BIN)/out_small.png 0.2 0" ../../bin/HalideTraceViz $@

viz_auto: $(BIN)/viz_auto.mp4
	$(HL_VIDEOPLAYER) $^

autotune: $(BIN)/bilateral_grid.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/bilateral_grid.generator \
		bilateral_grid \
		x86-64-avx2 \
		$(AUTOSCHED_SRC)/weights \
		$(AUTOSCHED_BIN)

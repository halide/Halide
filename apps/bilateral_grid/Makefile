include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

$(GENERATOR_BIN)/bilateral_grid.generator: bilateral_grid_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/bilateral_grid.a: $(GENERATOR_BIN)/bilateral_grid.generator
	@mkdir -p $(@D)
	$^ -g bilateral_grid -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilateral_grid target=$* auto_schedule=false

$(BIN)/%/bilateral_grid_auto_schedule.a: $(GENERATOR_BIN)/bilateral_grid.generator
	@mkdir -p $(@D)
	$^ -g bilateral_grid -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilateral_grid_auto_schedule target=$*-no_runtime auto_schedule=true -e static_library,h,schedule

$(BIN)/%/filter: filter.cpp $(BIN)/%/bilateral_grid.a $(BIN)/%/bilateral_grid_auto_schedule.a
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

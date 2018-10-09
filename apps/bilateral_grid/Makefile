include ../support/Makefile.inc

all: $(BIN)/filter

$(BIN)/bilateral_grid.generator: bilateral_grid_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/bilateral_grid.a: $(BIN)/bilateral_grid.generator
	@mkdir -p $(@D)
	$^ -g bilateral_grid -o $(BIN) -f bilateral_grid target=$(HL_TARGET) auto_schedule=false

$(BIN)/bilateral_grid_auto_schedule.a: $(BIN)/bilateral_grid.generator
	@mkdir -p $(@D)
	$^ -g bilateral_grid -o $(BIN) -f bilateral_grid_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true -e static_library,h,schedule

$(BIN)/viz/bilateral_grid.a: $(BIN)/bilateral_grid.generator
	@mkdir -p $(@D)
	$^ -g bilateral_grid -o $(BIN)/viz target=$(HL_TARGET)-trace_all

$(BIN)/filter: $(BIN)/bilateral_grid.a $(BIN)/bilateral_grid_auto_schedule.a filter.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -O3 -ffast-math -Wall -Werror -I$(BIN) filter.cpp $(BIN)/bilateral_grid.a $(BIN)/bilateral_grid_auto_schedule.a -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

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

clean:
	rm -rf $(BIN)

test: $(BIN)/out.png

viz: $(BIN)/bilateral_grid.mp4
	$(HL_VIDEOPLAYER) $^

$(BIN)/viz_auto.mp4: $(BIN)/filter_viz ../support/viz_auto.sh ../../bin/HalideTraceViz
	@mkdir -p $(@D)
	HL_AVCONV=$(HL_AVCONV) bash ../support/viz_auto.sh "$(BIN)/filter_viz $(IMAGES)/gray_small.png $(BIN)/out_small.png 0.2 0" ../../bin/HalideTraceViz $@

viz_auto: $(BIN)/viz_auto.mp4
	$(HL_VIDEOPLAYER) $^

include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/process

TIMING_ITERATIONS ?= 5

$(GENERATOR_BIN)/camera_pipe.generator: camera_pipe_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/camera_pipe.a: $(GENERATOR_BIN)/camera_pipe.generator
	@mkdir -p $(@D)
	$^ -g camera_pipe -o $(@D) -f camera_pipe target=$* auto_schedule=false

$(BIN)/%/camera_pipe_auto_schedule.a: $(GENERATOR_BIN)/camera_pipe.generator
	@mkdir -p $(@D)
	$^ -g camera_pipe -o $(@D) -f camera_pipe_auto_schedule target=$*-no_runtime auto_schedule=true

$(BIN)/%/process: process.cpp $(BIN)/$(HL_TARGET)/camera_pipe.a $(BIN)/$(HL_TARGET)/camera_pipe_auto_schedule.a
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

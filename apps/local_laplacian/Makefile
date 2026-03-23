include ../support/Makefile.inc

.PHONY: build clean test
.SECONDARY:

build: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/local_laplacian.generator: local_laplacian_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/local_laplacian.a: $(GENERATOR_BIN)/local_laplacian.generator
	@mkdir -p $(@D)
	$^ -g local_laplacian -e $(GENERATOR_OUTPUTS) -o $(@D) -f local_laplacian target=$*

$(BIN)/%/local_laplacian_auto_schedule.a: $(GENERATOR_BIN)/local_laplacian.generator
	@mkdir -p $(@D)
	$^ -g local_laplacian -e $(GENERATOR_OUTPUTS) -o $(@D) -f local_laplacian_auto_schedule target=$*-no_runtime autoscheduler=Mullapudi2016

$(BIN)/%/process: process.cpp $(BIN)/%/local_laplacian.a $(BIN)/%/local_laplacian_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb.png 8 1 1 10 $@

$(BIN)/%/out.tiff: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb.png 8 1 1 10 $@

$(BIN)/%/process_viz: process.cpp $(BIN)/%-trace_all/local_laplacian.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DNO_AUTO_SCHEDULE -I$(BIN)/$*-trace_all -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

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

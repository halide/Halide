include ../support/Makefile.inc

.PHONY: build clean test

build: $(BIN)/$(HL_TARGET)/process

TIMING_ITERATIONS ?= 5

$(GENERATOR_BIN)/camera_pipe.generator: camera_pipe_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/camera_pipe.a: $(GENERATOR_BIN)/camera_pipe.generator
	@mkdir -p $(@D)
	$^ -g camera_pipe -e $(GENERATOR_OUTPUTS) -o $(@D) -f camera_pipe target=$*

$(BIN)/%/camera_pipe_auto_schedule.a: $(GENERATOR_BIN)/camera_pipe.generator
	@mkdir -p $(@D)
	$^ -g camera_pipe -e $(GENERATOR_OUTPUTS) -o $(@D) -f camera_pipe_auto_schedule target=$*-no_runtime autoscheduler=Mullapudi2016

$(BIN)/%/process: process.cpp $(BIN)/%/camera_pipe.a $(BIN)/%/camera_pipe_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Wall -I$(BIN)/$* $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$(BIN)/$*/process $(IMAGES)/bayer_raw.png 3700 2.0 50 1.0 $(TIMING_ITERATIONS) $@ $(BIN)/$*/h_auto.png

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png

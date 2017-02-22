include ../support/Makefile.inc

all: $(BIN)/process

TIMING_ITERATIONS ?= 5

$(BIN)/camera_pipe_exec: camera_pipe_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/camera_pipe.a: $(BIN)/camera_pipe_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN) target=$(HL_TARGET)

$(BIN)/viz/camera_pipe.a: $(BIN)/camera_pipe_exec
	@-mkdir -p $(BIN)/viz
	HL_TRACE=3 $^ -o $(BIN)/viz target=$(HL_TARGET)

$(BIN)/Demosaic.o: fcam/Demosaic.cpp fcam/Demosaic.h
	$(CXX) $(CXXFLAGS) -c -Wall -O3 $< -o $@

$(BIN)/Demosaic_ARM.o: fcam/Demosaic_ARM.cpp fcam/Demosaic_ARM.h
	$(CXX) $(CXXFLAGS) -c -Wall -O3 $< -o $@

$(BIN)/process: process.cpp $(BIN)/camera_pipe.a $(BIN)/Demosaic.o $(BIN)/Demosaic_ARM.o
	$(CXX) $(CXXFLAGS) -Wall -O3 -I$(BIN) $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

$(BIN)/viz/process: process.cpp $(BIN)/viz/camera_pipe.a $(BIN)/Demosaic.o $(BIN)/Demosaic_ARM.o
	$(CXX) $(CXXFLAGS) -Wall -O3 -I$(BIN)/viz $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

$(BIN)/out.png: $(BIN)/process
	$(BIN)/process $(IMAGES)/bayer_raw.png 3700 2.0 50 $(TIMING_ITERATIONS) $@ $(BIN)/fcam_c.png $(BIN)/fcam_arm.png

../../bin/HalideTraceViz:
	$(MAKE) -C ../../ bin/HalideTraceViz

$(BIN)/camera_pipe.avi: $(BIN)/viz/process viz.sh $(HALIDE_TRACE_VIZ) ../../bin/HalideTraceViz
	bash viz.sh $(BIN)

clean:
	rm -rf $(BIN)

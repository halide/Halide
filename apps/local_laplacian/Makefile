include ../support/Makefile.inc

BIN ?= bin

all: $(BIN)/process

$(BIN)/local_laplacian_exec: local_laplacian_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/local_laplacian.a: $(BIN)/local_laplacian_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN)  target=$(HL_TARGET)

$(BIN)/process: process.cpp $(BIN)/local_laplacian.a
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/out.png: $(BIN)/process
	@-mkdir -p $(BIN)
	$(BIN)/process $(IMAGES)/rgb.png 8 1 1 10 $(BIN)/out.png

# Build rules for generating a visualization of the pipeline using HalideTraceViz
$(BIN)/viz/local_laplacian.a: $(BIN)/local_laplacian_exec
	@-mkdir -p $(BIN)/viz
	$^ -o $(BIN)/viz target=$(HL_TARGET)-trace_loads-trace_stores-trace_realizations pyramid_levels=6

$(BIN)/process_viz: process.cpp $(BIN)/viz/local_laplacian.a
	@-mkdir -p $(BIN)/viz
	$(CXX) $(CXXFLAGS) -I$(BIN)/viz -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

../../bin/HalideTraceViz:
	$(MAKE) -C ../../ bin/HalideTraceViz

$(BIN)/local_laplacian.mp4: $(BIN)/process_viz ../../bin/HalideTraceViz
	@-mkdir -p $(BIN)
	bash viz.sh

clean:
	rm -rf $(BIN)

include ../support/Makefile.inc

BIN ?= bin

all: $(BIN)/process

$(BIN)/resnet50.generator: Resnet50Generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/resnet50.a: $(BIN)/resnet50.generator
	@mkdir -p $(@D)
	$^ -g resnet50 -o $(BIN) -f resnet50 target=$(HL_TARGET) auto_schedule=false

$(BIN)/process: process.cpp $(BIN)/resnet50.a 
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)


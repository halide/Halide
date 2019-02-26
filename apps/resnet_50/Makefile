include ../support/Makefile.inc

BIN ?= bin
SEED = 123

all: $(BIN)/process

$(BIN)/pytorch_weights/ok:
	@mkdir -p $(@D)
	python3 load_weights.py $(@D)
	echo "ok" > $@

$(BIN)/resnet50.generator: Resnet50Generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/resnet50.a: $(BIN)/resnet50.generator
	@mkdir -p $(@D)
	$^ -g resnet50 -o $(BIN) -f resnet50 target=$(HL_TARGET) auto_schedule=false

$(BIN)/process: process.cpp $(BIN)/resnet50.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

benchmark_and_validate: $(BIN)/process $(BIN)/pytorch_weights/ok
	$< 10 $* $(BIN)/pytorch_weights/ $(SEED) $(BIN)/res50gen_output.bin
	python3 validate_resnet50_output.py $(BIN)/res50gen_output.bin $(SEED)

clean:
	rm -rf $(BIN)

test: $(BIN)/process

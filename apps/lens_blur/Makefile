include ../support/Makefile.inc

BIN ?= bin

all: $(BIN)/process

$(BIN)/lens_blur.generator: lens_blur_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/lens_blur.a: $(BIN)/lens_blur.generator
	@-mkdir -p $(BIN)
	$^ -g lens_blur -o $(BIN) -f lens_blur target=$(HL_TARGET) auto_schedule=false

$(BIN)/lens_blur_auto_schedule.a: $(BIN)/lens_blur.generator
	@-mkdir -p $(BIN)
	$^ -g lens_blur -o $(BIN) -f lens_blur_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true

$(BIN)/process: process.cpp $(BIN)/lens_blur.a $(BIN)/lens_blur_auto_schedule.a
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/out.png: $(BIN)/process
	@-mkdir -p $(BIN)
	$(BIN)/process $(IMAGES)/rgb_small.png 32 13 0.5 32 3 $(BIN)/out.png

clean:
	rm -rf $(BIN)

test: $(BIN)/out.png

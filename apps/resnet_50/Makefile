include ../support/Makefile.inc

SEED = 123

.PHONY: build clean test

build: $(BIN)/$(HL_TARGET)/process

$(BIN)/%/pytorch_weights/ok:
	@mkdir -p $(@D)
	python3 load_weights.py $(@D)
	echo "ok" > $@

$(GENERATOR_BIN)/resnet_50.generator: Resnet50Generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS) 

$(BIN)/%/resnet_50.a: $(GENERATOR_BIN)/resnet_50.generator
	@mkdir -p $(@D)
	$^ -g resnet_50 -o $(@D) -f resnet_50 target=$* auto_schedule=false

$(BIN)/%/process: process.cpp $(BIN)/%/resnet_50.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

benchmark_and_validate: $(BIN)/$(HL_TARGET)/process $(BIN)/$(HL_TARGET)/pytorch_weights/ok
	$< 10 $* $(BIN)/$(HL_TARGET)/pytorch_weights/ $(SEED) $(BIN)/$(HL_TARGET)/res50gen_output.bin
	python3 validate_resnet50_output.py $(BIN)/$(HL_TARGET)/res50gen_output.bin $(SEED)

clean:
	rm -rf $(BIN)

test:
	@echo "Only currently testing that resnet builds"

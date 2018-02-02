include ../support/Makefile.inc

BIN ?= bin

all: $(BIN)/process

$(BIN)/stencil_chain_exec: stencil_chain_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/stencil_chain.a: $(BIN)/stencil_chain_exec
	@mkdir -p $(@D)
	$^ -g stencil_chain -o $(BIN) -f stencil_chain target=$(HL_TARGET) auto_schedule=false stencils=16

$(BIN)/stencil_chain_auto_schedule_old.a: $(BIN)/stencil_chain_exec
	@mkdir -p $(@D)
	$^ -g stencil_chain -o $(BIN) -f stencil_chain_auto_schedule_old target=$(HL_TARGET)-auto_schedule_old-no_runtime auto_schedule=true stencils=8

$(BIN)/stencil_chain_auto_schedule.a: $(BIN)/stencil_chain_exec
	@mkdir -p $(@D)
	$^ -g stencil_chain -o $(BIN) -f stencil_chain_auto_schedule target=$(HL_TARGET)-no_runtime-auto_schedule_top_down auto_schedule=true stencils=8

$(BIN)/process: process.cpp $(BIN)/stencil_chain.a $(BIN)/stencil_chain_auto_schedule_old.a $(BIN)/stencil_chain_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/out.png: $(BIN)/process
	@mkdir -p $(@D)
	$(BIN)/process $(IMAGES)/rgb.png 100 $(BIN)/out.png

clean:
	rm -rf $(BIN)

include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin

all: $(BIN)/process

$(BIN)/stencil_chain.generator: stencil_chain_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/stencil_chain.a: $(BIN)/stencil_chain.generator
	@mkdir -p $(@D)
	$^ -g stencil_chain -o $(BIN) -f stencil_chain target=$(HL_TARGET) auto_schedule=false

$(BIN)/stencil_chain_classic_auto_schedule.a: $(BIN)/stencil_chain.generator
	@mkdir -p $(@D)
	$< -g stencil_chain -o $(BIN) -f stencil_chain_classic_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true machine_params=$(HL_MACHINE_PARAMS)

$(BIN)/stencil_chain_auto_schedule.a: $(BIN)/stencil_chain.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/weights HL_PERMIT_FAILED_UNROLL=1 \
	$< -g stencil_chain -o $(BIN) -f stencil_chain_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so -e stmt,assembly,static_library,h

$(BIN)/process: process.cpp $(BIN)/stencil_chain.a $(BIN)/stencil_chain_classic_auto_schedule.a $(BIN)/stencil_chain_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/out.png: $(BIN)/process
	@mkdir -p $(@D)
	$(BIN)/process $(IMAGES)/rgb.png 10 $(BIN)/out.png

clean:
	rm -rf $(BIN)

test: $(BIN)/out.png

autotune: $(BIN)/stencil_chain.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/stencil_chain.generator \
		stencil_chain \
		x86-64-avx2 \
		$(AUTOSCHED_SRC)/weights \
		$(AUTOSCHED_BIN)

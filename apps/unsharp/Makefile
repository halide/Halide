include ../support/Makefile.inc
include ../support/autoscheduler.inc

all: $(BIN)/test

$(BIN)/unsharp.generator: unsharp_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/unsharp.a: $(BIN)/unsharp.generator
	@mkdir -p $(@D)
	$< -g unsharp -f unsharp -o $(BIN) target=$(HL_TARGET) auto_schedule=false

$(BIN)/unsharp_classic_auto_schedule.a: $(BIN)/unsharp.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	$< -g unsharp -f unsharp_classic_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true machine_params=$(HL_MACHINE_PARAMS)

$(BIN)/unsharp_auto_schedule.a: $(BIN)/unsharp.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=$(AUTOSCHED_WEIGHTS_DIR) \
	$< -g unsharp -f unsharp_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so

$(BIN)/filter: filter.cpp $(BIN)/unsharp.a $(BIN)/unsharp_auto_schedule.a $(BIN)/unsharp_classic_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)

test: $(BIN)/filter
	$(BIN)/filter ../images/rgb.png $(BIN)/out.tiff


autotune: $(BIN)/unsharp.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/unsharp.generator \
		unsharp \
		x86-64-avx2 \
		$(AUTOSCHED_WEIGHTS_DIR) \
		$(AUTOSCHED_BIN)

include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin

$(BIN)/iir_blur.generator: iir_blur_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/iir_blur.a: $(BIN)/iir_blur.generator
	@mkdir -p $(@D)
	$< -g iir_blur -f iir_blur -o $(BIN) target=$(HL_TARGET) auto_schedule=false

$(BIN)/iir_blur_classic_auto_schedule.a: $(BIN)/iir_blur.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	$< -g iir_blur -f iir_blur_classic_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true

$(BIN)/iir_blur_auto_schedule.a: $(BIN)/iir_blur.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/weights \
	$< -g iir_blur -f iir_blur_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so

$(BIN)/process: process.cpp $(BIN)/iir_blur.a $(BIN)/iir_blur_auto_schedule.a $(BIN)/iir_blur_classic_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)

test: $(BIN)/process
	$(BIN)/process

autotune: $(BIN)/iir_blur.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/iir_blur.generator \
		iir_blur \
		x86-64-avx2 \
		$(AUTOSCHED_SRC)/weights \
		$(AUTOSCHED_BIN)

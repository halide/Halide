include ../support/Makefile.inc
include ../support/autoscheduler.inc

all: $(BIN)/test

$(BIN)/interpolate.generator: interpolate_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/interpolate.a: $(BIN)/interpolate.generator
	@mkdir -p $(@D)
	$< -g interpolate -f interpolate -o $(BIN) target=$(HL_TARGET) auto_schedule=false

$(BIN)/interpolate_classic_auto_schedule.a: $(BIN)/interpolate.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	$< -g interpolate -f interpolate_classic_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true machine_params=$(HL_MACHINE_PARAMS)

$(BIN)/interpolate_auto_schedule.a: $(BIN)/interpolate.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=$(AUTOSCHED_WEIGHTS_DIR) \
	$< -g interpolate -f interpolate_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so

$(BIN)/filter: filter.cpp $(BIN)/interpolate.a $(BIN)/interpolate_auto_schedule.a $(BIN)/interpolate_classic_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)

test: $(BIN)/filter
	$(BIN)/filter ../images/rgba.png $(BIN)/out.png

autotune: $(BIN)/interpolate.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/interpolate.generator \
		interpolate \
		x86-64-avx2 \
		$(AUTOSCHED_WEIGHTS_DIR) \
		$(AUTOSCHED_BIN)

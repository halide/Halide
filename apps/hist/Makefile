include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

all: $(BIN)/test

$(BIN)/hist.generator: hist_generator.cpp ../autoscheduler/SimpleAutoSchedule.h ../autoscheduler/SimpleAutoSchedule.cpp ../autoscheduler/DerivativeUtils.h ../autoscheduler/DerivativeUtils.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/hist.a: $(BIN)/hist.generator
	@mkdir -p $(@D)
	$< -g hist -f hist -o $(BIN) target=$(HL_TARGET) auto_schedule=false

$(BIN)/hist_classic_auto_schedule.a: $(BIN)/hist.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=24 \
	HL_COMPUTE_ROOT_AND_INLINE_ONLY=1 \
	$< -g hist -f hist_classic_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) -e static_library,h,assembly,stmt 2> stderr_classic.txt > stdout_classic.txt

$(BIN)/hist_auto_schedule.a: $(BIN)/hist.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/gpu_weights \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=24 \
	$< -g hist -f hist_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so machine_params=$(HL_MACHINE_PARAMS) 2> stderr.txt > stdout.txt

$(BIN)/hist_simple_auto_schedule.a: $(BIN)/hist.generator
	@mkdir -p $(@D)
	HL_USE_SIMPLE_AUTOSCHEDULER=1 \
	$< -g hist -o $(BIN) -f hist_simple_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=false -e static_library,h

$(BIN)/filter: filter.cpp $(BIN)/hist.a $(BIN)/hist_auto_schedule.a $(BIN)/hist_classic_auto_schedule.a $(BIN)/hist_simple_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)

test: $(BIN)/filter
	$(BIN)/filter ../images/rgb.png $(BIN)/out.png

autotune: $(BIN)/hist.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/hist.generator \
		hist \
		x86-64-avx2 \
		$(AUTOSCHED_SRC)/weights \
		$(AUTOSCHED_BIN)


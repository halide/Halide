include ../support/Makefile.inc
include ../support/autoscheduler.inc

BIN ?= bin

all: $(BIN)/process

$(BIN)/lens_blur.generator: lens_blur_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/lens_blur.a: $(BIN)/lens_blur.generator
	@-mkdir -p $(BIN)
	$< -g lens_blur -o $(BIN) -f lens_blur target=$(HL_TARGET)-large_buffers auto_schedule=false

$(BIN)/lens_blur_classic_auto_schedule.a: $(BIN)/lens_blur.generator
	@-mkdir -p $(BIN)
	$< -g lens_blur -o $(BIN) -f lens_blur_classic_auto_schedule target=$(HL_TARGET)-large_buffers-no_runtime auto_schedule=true machine_params=$(HL_MACHINE_PARAMS)

$(BIN)/lens_blur_auto_schedule.a: $(BIN)/lens_blur.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@-mkdir -p $(BIN)
	HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=$(AUTOSCHED_WEIGHTS_DIR) \
	$< -g lens_blur -o $(BIN) -f lens_blur_auto_schedule target=$(HL_TARGET)-large_buffers-no_runtime auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so -e static_library,h,assembly,stmt,registration

$(BIN)/process: process.cpp $(BIN)/lens_blur.a $(BIN)/lens_blur_classic_auto_schedule.a $(BIN)/lens_blur_auto_schedule.a
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/out.png: $(BIN)/process
	@-mkdir -p $(BIN)
	$(BIN)/process $(IMAGES)/rgb.png 32 13 0.5 32 3 $(BIN)/out.png

clean:
	rm -rf $(BIN)

test: $(BIN)/out.png

autotune: $(BIN)/lens_blur.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/lens_blur.generator \
		lens_blur \
		x86-64-avx2-large_buffers \
		$(AUTOSCHED_WEIGHTS_DIR) \
		$(AUTOSCHED_BIN)

include ../support/Makefile.inc
include ../support/autoscheduler.inc

GRADIENT_AUTOSCHED_BIN=../gradient_autoscheduler/bin

CXXFLAGS += -I../support/

PROCESS_DEPS=$(BIN)/%/multires_demosaic.a

ifndef NO_AUTO_SCHEDULE
	PROCESS_DEPS += $(BIN)/%/multires_demosaic_gradient_auto_schedule.a
else
	CXXFLAGS += -DNO_AUTO_SCHEDULE
endif

.PHONY: build clean test

build: $(BIN)/$(HL_TARGET)/process

$(GENERATOR_BIN)/multires_demosaic.generator: multires_demosaic_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	@echo $(CXXFLAGS)
	@echo $(CXX) cxx
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS) 

$(BIN)/%/multires_demosaic.a: $(GENERATOR_BIN)/multires_demosaic.generator
	@mkdir -p $(@D)
	$^ -g multires_demosaic -e $(GENERATOR_OUTPUTS) -o $(@D) -f multires_demosaic target=$* auto_schedule=false

$(BIN)/%/multires_demosaic_gradient_auto_schedule.a: $(GENERATOR_BIN)/multires_demosaic.generator
	@mkdir -p $(@D)
	$^ -g multires_demosaic -e $(GENERATOR_OUTPUTS) -o $(@D) -f multires_demosaic_gradient_auto_schedule target=$*-no_runtime auto_schedule=true -p $(GRADIENT_AUTOSCHED_BIN)/libgradient_autoscheduler.so -s Li2018

$(BIN)/%/process: process.cpp $(PROCESS_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(@D) -Wall $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/process
	@mkdir -p $(@D)
	$< $(IMAGES)/rgb.png 10 $@
	rm $@

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png

BATCH_ID ?= 0
TRAIN_ONLY ?= 0
SAMPLES_DIR ?= autotuned_samples
autotune: $(GENERATOR_BIN)/multires_demosaic.generator $(AUTOSCHED_BIN)/featurization_to_sample $(AUTOSCHED_BIN)/retrain_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	HL_MACHINE_PARAMS=80,1,1 \
	SAMPLES_DIR=$(SAMPLES_DIR) \
	HL_DEBUG_CODEGEN=0 \
	HL_SHARED_MEMORY_LIMIT=48 \
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(GENERATOR_BIN)/multires_demosaic.generator \
		multires_demosaic \
		$(HL_TARGET) \
		$(AUTOSCHED_SRC)/gpu.weights \
		$(AUTOSCHED_BIN) \
		$(BATCH_ID) \
		$(TRAIN_ONLY)

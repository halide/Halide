include ../support/Makefile.inc
include ../support/autoscheduler.inc

all: $(BIN)/test

$(BIN)/hist.generator: hist_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/hist.a: $(BIN)/hist.generator
	@mkdir -p $(@D)
	$< -g hist -f hist -o $(BIN) target=$(HL_TARGET) auto_schedule=false

$(BIN)/hist_classic_auto_schedule.a: $(BIN)/hist.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	$< -g hist -f hist_classic_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true

$(BIN)/hist_auto_schedule.a: $(BIN)/hist.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/weights HL_MACHINE_PARAMS=32,1,1 \
	$< -g hist -f hist_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so

$(BIN)/filter: filter.cpp $(BIN)/hist.a $(BIN)/hist_auto_schedule.a $(BIN)/hist_classic_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

clean:
	rm -rf $(BIN)

test: $(BIN)/filter
	$(BIN)/filter ../images/rgb.png $(BIN)/out.png $(BIN)/out-classic.png $(BIN)/out-auto.png



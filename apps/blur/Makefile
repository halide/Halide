include ../support/Makefile.inc
include ../support/autoscheduler.inc

all: $(BIN)/test

$(BIN)/halide_blur.generator: halide_blur_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/%/halide_blur.a: $(BIN)/halide_blur.generator
	@mkdir -p $(@D)
	$^ -g halide_blur -o $(BIN)/$* target=$(HL_TARGET)

$(BIN)/%/halide_blur_classic_auto_schedule.a: $(BIN)/halide_blur.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	$< -g halide_blur -f halide_blur_classic_auto_schedule -o $(BIN)/$* target=$(HL_TARGET) auto_schedule=true machine_params=$(HL_MACHINE_PARAMS)

$(BIN)/%/halide_blur_auto_schedule.a: $(BIN)/halide_blur.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=$(AUTOSCHED_WEIGHTS_DIR) \
	$< -g halide_blur -f halide_blur_auto_schedule -o $(BIN)/$* target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so

# g++ on OS X might actually be system clang without openmp
CXX_VERSION=$(shell $(CXX) --version)
ifeq (,$(findstring clang,$(CXX_VERSION)))
OPENMP_FLAGS=-fopenmp
else
OPENMP_FLAGS=
endif

# -O2 is faster than -O3 for this app (O3 unrolls too much)
$(BIN)/%/test: test.cpp $(BIN)/%/halide_blur.a $(BIN)/%/halide_blur_auto_schedule.a $(BIN)/%/halide_blur_classic_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(OPENMP_FLAGS) -Wall -O2 -I$(BIN)/$* $^ -o $@ $(LDFLAGS-$*)

clean:
	rm -rf $(BIN)

test: $(BIN)/host/test
	$(BIN)/host/test

autotune: $(BIN)/halide_blur.generator $(AUTOSCHED_BIN)/augment_sample $(AUTOSCHED_BIN)/train_cost_model $(AUTOSCHED_BIN)/libauto_schedule.so $(AUTOSCHED_SRC)/autotune_loop.sh
	bash $(AUTOSCHED_SRC)/autotune_loop.sh \
		$(BIN)/halide_blur.generator \
		halide_blur \
		x86-64-avx2 \
		$(AUTOSCHED_WEIGHTS_DIR) \
		$(AUTOSCHED_BIN)

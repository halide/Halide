include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

all: $(BIN)/test

$(BIN)/halide_blur.generator: halide_blur_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(BIN)/libhalide_blur.a: $(BIN)/halide_blur.generator
	@mkdir -p $(@D)
	$< -g halide_blur -o $(BIN) target=$(HL_TARGET) auto_schedule=false
	@mv $(BIN)/halide_blur.a $(BIN)/libhalide_blur.a

# g++ on OS X might actually be system clang without openmp
CXX_VERSION=$(shell $(CXX) --version)
ifeq (,$(findstring clang,$(CXX_VERSION)))
OPENMP_FLAGS=-fopenmp
else
OPENMP_FLAGS=
endif

$(BIN)/blur_classic_auto_schedule.a: $(BIN)/halide_blur.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	$< -g halide_blur -f blur_classic_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true

$(BIN)/blur_auto_schedule.a: $(BIN)/halide_blur.generator $(AUTOSCHED_BIN)/libauto_schedule.so
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 \
	HL_WEIGHTS_DIR=$(AUTOSCHED_SRC)/weights \
	HL_MACHINE_PARAMS=1000,1,1 \
	HL_DEBUG_CODEGEN=1 \
	HL_SHARED_MEMORY_LIMIT=24 \
	$< -g halide_blur -f blur_auto_schedule -o $(BIN) target=$(HL_TARGET) auto_schedule=true -p $(AUTOSCHED_BIN)/libauto_schedule.so 2> stderr.txt > stdout.txt

# -O2 is faster than -O3 for this app (O3 unrolls too much)
$(BIN)/test: $(BIN)/libhalide_blur.a test.cpp $(BIN)/blur_auto_schedule.a $(BIN)/blur_classic_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -Wall -O2 -I$(BIN) $^ -L./bin -lhalide_blur -o $(BIN)/test $(LDFLAGS) $(CUDA_LDFLAGS)

clean:
	rm -rf $(BIN)

test: $(BIN)/test
	$(BIN)/test

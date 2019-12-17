include ../support/Makefile.inc
include ../support/autoscheduler.inc

HL_TARGET=host-cuda

all: $(BIN)/$(HL_TARGET)/test

$(GENERATOR_BIN)/halide_blur.generator: halide_blur_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(GENERATOR_LDFLAGS)

$(BIN)/%/halide_blur.a: $(GENERATOR_BIN)/halide_blur.generator
	@mkdir -p $(@D)
	$^ -g halide_blur -e $(GENERATOR_OUTPUTS) -o $(@D) target=$* auto_schedule=false
	@mv $(BIN)/halide_blur.a $(BIN)/libhalide_blur.a

# g++ on OS X might actually be system clang without openmp
CXX_VERSION=$(shell $(CXX) --version)
ifeq (,$(findstring clang,$(CXX_VERSION)))
OPENMP_FLAGS=-fopenmp
else
OPENMP_FLAGS=
endif

# -O2 is faster than -O3 for this app (O3 unrolls too much)
$(BIN)/test: $(BIN)/libhalide_blur.a test.cpp 
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -Wall -O2 -I$(BIN) $^ -L./bin -lhalide_blur -o $(BIN)/test $(LDFLAGS) $(CUDA_LDFLAGS)

clean:
	rm -rf $(BIN)

.PHONY: test
test: $(BIN)/$(HL_TARGET)/test
	$(BIN)/$(HL_TARGET)/test

# halide.make requires that HALIDE_DISTRIB_DIR be set to point to the Halide distribution folder we use.
# This assumes it's built at the toplevel via 'make distrib'.
HALIDE_DISTRIB_DIR ?= ../../distrib

include $(HALIDE_DISTRIB_DIR)/halide.make

all: test

clean:
	@rm -rf $(BIN)

# By default, %.generator is produced by building %_generator.cpp
$(BIN)/%.generator: %_generator.cpp $(HALIDE_GENERATOR_DEPS)
	@echo Building Generator $(filter %_generator.cpp,$^)
	@mkdir -p $(@D)
	@$(CXX) $(filter-out %.h,$^) $(HALIDE_GENERATOR_CXXFLAGS) $(HALIDE_GENERATOR_LDFLAGS) -o $@

# By default, %.a/.h are produced by executing %.generator
$(BIN)/%.a $(BIN)/%.h: $(BIN)/%.generator
	@echo Running Generator $<
	@mkdir -p $(@D)
	@$< -g $(*F) -f $(*F) -o $(BIN) target=$(HL_TARGET)-no_runtime

# Since we build with the -no_runtime flag, add a rule to build just-the-runtime separately.
$(BIN)/runtime.generator: $(HALIDE_GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(HALIDE_GENERATOR_CXXFLAGS) $(filter-out %.h,$^) $(HALIDE_GENERATOR_LDFLAGS) -o $@

$(BIN)/runtime.a: $(BIN)/runtime.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=$(HL_TARGET)

# g++ on OS X might actually be system clang without openmp
CXX_VERSION=$(shell $(CXX) --version)
ifeq (,$(findstring clang,$(CXX_VERSION)))
OPENMP_FLAGS=-fopenmp
else
OPENMP_FLAGS=
endif

# -O2 is faster than -O3 for this app (O3 unrolls too much)
EXTRA_CXX_FLAGS = $(OPENMP_FLAGS) -msse2 -Wall -O2  

MODULES = \
	$(BIN)/halide_blur.a \
	$(BIN)/runtime.a

$(BIN)/halide_blur_test: halide_blur_test.cpp $(MODULES)
	$(CXX) $(CXXFLAGS) $(HALIDE_IMAGE_IO_CXXFLAGS) $(EXTRA_CXX_FLAGS) -I$(BIN) $^ $(MODULES) $(LDFLAGS) $(HALIDE_IMAGE_IO_LDFLAGS) -o $@

test: $(BIN)/halide_blur_test
	@echo Testing $<...
	@$< 

# Don't auto-delete the generators.
.SECONDARY:

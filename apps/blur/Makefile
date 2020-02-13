include ../support/Makefile.inc

.PHONY: build clean test
build: $(BIN)/$(HL_TARGET)/test

# In order to ensure our static library works, we arbitrarily link against
# the static library for this app.
$(GENERATOR_BIN)/halide_blur.generator: halide_blur_generator.cpp $(GENERATOR_DEPS_STATIC)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(GENERATOR_LDFLAGS_STATIC)

$(BIN)/%/halide_blur.a: $(GENERATOR_BIN)/halide_blur.generator
	@mkdir -p $(@D)
	$^ -g halide_blur -e $(GENERATOR_OUTPUTS) -o $(@D) target=$*

# g++ on OS X might actually be system clang without openmp
CXX_VERSION=$(shell $(CXX) --version)
ifeq (,$(findstring clang,$(CXX_VERSION)))
OPENMP_FLAGS=-fopenmp
else
OPENMP_FLAGS=
endif

# -O2 is faster than -O3 for this app (O3 unrolls too much)
$(BIN)/%/test: $(BIN)/%/halide_blur.a test.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(OPENMP_FLAGS) -Wall -O2 -I$(BIN)/$* test.cpp $(BIN)/$*/halide_blur.a -o $@ $(LDFLAGS-$*)

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/test
	$<

include ../support/Makefile.inc

.PHONY: build clean test
build: $(BIN)/$(HL_TARGET)/test

# In order to ensure our static library works, we arbitrarily link against
# the static library for this app.
$(GENERATOR_BIN)/halide_matmul64x64.generator: halide_matmul64x64_generator.cpp $(GENERATOR_DEPS_STATIC)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS_STATIC)

$(BIN)/%/halide_matmul64x64.a: $(GENERATOR_BIN)/halide_matmul64x64.generator
	@mkdir -p $(@D)
	$^ -g halide_matmul64x64 -e $(GENERATOR_OUTPUTS) -o $(@D) target=$*

$(BIN)/%/halide_matmul64x64_c.halide_generated.cpp: $(GENERATOR_BIN)/halide_matmul64x64.generator
	@mkdir -p $(@D)
	$^ -g halide_matmul64x64 -o $(@D) -f halide_matmul64x64_c -e c_source,c_header target=$*-xtensa

# g++ on OS X might actually be system clang without openmp
CXX_VERSION=$(shell $(CXX) --version)
ifeq (,$(findstring clang,$(CXX_VERSION)))
OPENMP_FLAGS=-fopenmp
else
OPENMP_FLAGS=
endif

# -O2 is faster than -O3 for this app (O3 unrolls too much)
$(BIN)/%/test: $(BIN)/%/halide_matmul64x64.a $(BIN)/%/halide_matmul64x64_c.halide_generated.cpp test.cpp
	@mkdir -p $(@D)
	$(CXX-$*) $(CXXFLAGS-$*) $(OPENMP_FLAGS) -Wall -O2 -I$(BIN)/$* -I${XTENSA_CSTUBS_ROOT}  test.cpp $(BIN)/$*/halide_matmul64x64_c.halide_generated.cpp $(BIN)/$*/halide_matmul64x64.a ${XTENSA_CSTUBS_ROOT}/libcstub.a -o $@ $(LDFLAGS-$*)

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/test
	$<

.SECONDARY: $(BIN)/host/halide_matmul64x64_c.halide_generated.cpp

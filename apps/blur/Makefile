include ../support/Makefile.inc

all: $(BIN)/test

$(BIN)/halide_blur_exec: halide_blur_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/halide_blur.a: $(BIN)/halide_blur_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN) target=$(HL_TARGET)

# g++ on OS X might actually be system clang without openmp
CXX_VERSION=$(shell $(CXX) --version)
ifeq (,$(findstring clang,$(CXX_VERSION)))
OPENMP_FLAGS=-fopenmp
else
OPENMP_FLAGS=
endif

# -O2 is faster than -O3 for this app (O3 unrolls too much)
$(BIN)/test: $(BIN)/halide_blur.a test.cpp
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -msse2 -Wall -O2 -I$(BIN) test.cpp $(BIN)/halide_blur.a -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS)

clean:
	rm -rf $(BIN)

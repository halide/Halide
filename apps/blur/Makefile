include ../support/Makefile.inc

all: $(BIN)/test

$(BIN)/halide_blur_exec: halide_blur_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/halide_blur.a: $(BIN)/halide_blur_exec
	@mkdir -p $(@D)
	$^ -g halide_blur -o $(BIN) -f halide_blur target=$(HL_TARGET) auto_schedule=false

$(BIN)/halide_blur_auto_schedule.a: $(BIN)/halide_blur_exec
	@mkdir -p $(@D)
	$^ -g halide_blur -o $(BIN) -f halide_blur_auto_schedule target=$(HL_TARGET) auto_schedule=true


# g++ on OS X might actually be system clang without openmp
CXX_VERSION=$(shell $(CXX) --version)
ifeq (,$(findstring clang,$(CXX_VERSION)))
OPENMP_FLAGS=-fopenmp
else
OPENMP_FLAGS=
endif

# -O2 is faster than -O3 for this app (O3 unrolls too much)
$(BIN)/test: test.cpp $(BIN)/halide_blur.a $(BIN)/halide_blur_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(OPENMP_FLAGS) -msse2 -Wall -O2 -I$(BIN) test.cpp $(BIN)/halide_blur.a $(BIN)/halide_blur_auto_schedule.a -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS)

run: $(BIN)/test
	@-mkdir -p $(BIN)
	$(BIN)/test

clean:
	rm -rf $(BIN)

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

MODULES = \
	$(BIN)/daubechies_x.a \
	$(BIN)/haar_x.a \
	$(BIN)/inverse_daubechies_x.a \
	$(BIN)/inverse_haar_x.a \
	$(BIN)/runtime.a

$(BIN)/wavelet: wavelet.cpp $(MODULES)
	$(CXX) $(CXXFLAGS) $(HALIDE_IMAGE_IO_CXXFLAGS) -I$(BIN) $^ $(MODULES) $(LDFLAGS) $(HALIDE_IMAGE_IO_LDFLAGS) -o $@

IMAGES ?= ../images

test: $(BIN)/wavelet
	@echo Testing $<...
	@$< $(IMAGES)/gray.png $(BIN)

# Don't auto-delete the generators.
.SECONDARY:

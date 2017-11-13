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

# By default, %_auto_schedule.a/.h are produced by executing %.generator with auto_schedule=true
$(BIN)/%_auto_schedule.a $(BIN)/%_auto_schedule.h: $(BIN)/%.generator
	@echo Running Generator $< with auto_schedule=true
	@mkdir -p $(@D)
	@$< -g $(*F) -f $(*F)_auto_schedule -o $(BIN) target=$(HL_TARGET)-no_runtime auto_schedule=true

MODULES = \
	$(BIN)/conv_layer.a \
	$(BIN)/conv_layer_auto_schedule.a \
	$(BIN)/runtime.a

$(BIN)/process: process.cpp $(MODULES)
	$(CXX) $(CXXFLAGS) $(HALIDE_IMAGE_IO_CXXFLAGS) -I$(BIN) $^ $(MODULES) $(LDFLAGS) $(HALIDE_IMAGE_IO_LDFLAGS) -o $@

test: $(BIN)/process
	@echo Testing $<...
	@$< 


# halide.make requires that HALIDE_DISTRIB_DIR be set to point to the Halide distribution folder we use.
# This assumes it's built at the toplevel via 'make distrib'.
HALIDE_DISTRIB_DIR ?= ../../distrib

include $(HALIDE_DISTRIB_DIR)/halide.make

all: test

clean:
	@rm -rf $(BIN)

test: $(BIN)/run $(BIN)/run_cpp
	@echo Testing $<...
	$(BIN)/run
	$(BIN)/run_cpp

# By default, %.generator is produced by building %_generator.cpp
$(BIN)/%.generator: %_generator.cpp $(HALIDE_GENERATOR_DEPS)
	@echo Building Generator $(filter %_generator.cpp,$^)
	@mkdir -p $(@D)
	@$(CXX) $(filter-out %.h,$^) $(HALIDE_GENERATOR_CXXFLAGS) $(HALIDE_GENERATOR_LDFLAGS) -o $@

# "normal" Generator outputs
$(BIN)/%_native.a $(BIN)/%_native.h: $(BIN)/%.generator
	@echo Running Generator $< for native output
	@mkdir -p $(@D)
	@$< -g $(*F) -f $(*F)_native -o $(BIN) target=$(HL_TARGET)-c_plus_plus_name_mangling

# C-code Generator outputs (no C++ name mangling)
$(BIN)/%_c.cpp $(BIN)/%_c.h: $(BIN)/%.generator
	@echo Running Generator $< for C output
	@mkdir -p $(@D)
	@$< -g $(*F) -f $(*F)_c -o $(BIN) -e cpp,h target=$(HL_TARGET)

# C++-code Generator outputs
$(BIN)/%_cpp.cpp $(BIN)/%_cpp.h: $(BIN)/%.generator
	@echo Running Generator $< for C++ output
	@mkdir -p $(@D)
	@$< -g $(*F) -f $(*F)_cpp -o $(BIN) -e cpp,h target=$(HL_TARGET)-c_plus_plus_name_mangling

$(BIN)/run: run.cpp $(BIN)/pipeline_c.cpp $(BIN)/pipeline_native.a
	$(CXX) $(CXXFLAGS) -Wall -I$(BIN) $(filter-out %.h,$^) -o $@  $(LDFLAGS)

$(BIN)/run_cpp: run_cpp.cpp $(BIN)/pipeline_cpp_cpp.cpp $(BIN)/pipeline_cpp_native.a
	$(CXX) $(CXXFLAGS) -Wall -I$(BIN) $(filter-out %.h,$^) -o $@  $(LDFLAGS)

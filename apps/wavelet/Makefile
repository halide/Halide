include ../support/Makefile.inc

BUILD_DIR = build_make

# If HL_TARGET isn't set, use host
HL_TARGET ?= host

all: $(BUILD_DIR)/wavelet

clean:
	@rm -rf $(BUILD_DIR)

# By default, %.generator is produced by building %_generator.cpp
$(BUILD_DIR)/%.generator: %_generator.cpp $(GENERATOR_DEPS)
	@echo Building Generator $(filter %_generator.cpp,$^)
	@mkdir -p $(BUILD_DIR)
	@$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) $(LDFLAGS) -o $@

# By default, %.a/.h are produced by executing %.generator
$(BUILD_DIR)/%.a $(BUILD_DIR)/%.h: $(BUILD_DIR)/%.generator
	@echo Running Generator $<
	@mkdir -p $(BUILD_DIR)
	@$< -g $(notdir $*) -o $(BUILD_DIR) target=$(HL_TARGET)-no_runtime

$(BUILD_DIR)/runtime_$(HL_TARGET).a: $(BUILD_DIR)/haar_x.generator
	@echo Compiling Halide runtime for target $(HL_TARGET)
	@mkdir -p $(BUILD_DIR)
	@$< -r runtime_$(HL_TARGET) -o $(BUILD_DIR) target=$(HL_TARGET)

HL_MODULES = \
	$(BUILD_DIR)/daubechies_x.a \
	$(BUILD_DIR)/haar_x.a \
	$(BUILD_DIR)/inverse_daubechies_x.a \
	$(BUILD_DIR)/inverse_haar_x.a \
	$(BUILD_DIR)/runtime_$(HL_TARGET).a

$(BUILD_DIR)/wavelet.a: wavelet.cpp $(HL_MODULES)
	@$(CXX) $(CXXFLAGS) $(LIBPNG_CXX_FLAGS) -I$(BUILD_DIR) -c $< -o $@

$(BUILD_DIR)/wavelet: $(BUILD_DIR)/wavelet.a
	@$(CXX) $(CXXFLAGS) $^ $(HL_MODULES) $(PNGFLAGS) $(LDFLAGS) -o $@

test: $(BUILD_DIR)/wavelet
	@echo Testing wavelet...
	@$< ../images/gray.png $(BUILD_DIR)

# Don't auto-delete the generators.
.SECONDARY:

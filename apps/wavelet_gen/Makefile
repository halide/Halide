include ../support/Makefile.inc

ifdef BUILD_PREFIX
BUILD_DIR = build_make/$(BUILD_PREFIX)
else
BUILD_DIR = build_make
endif

# If HL_TARGET isn't set, use host
HL_TARGET ?= host

all: $(BUILD_DIR)/wavelet_gen

clean:
	@rm -rf $(BUILD_DIR)

# By default, %.generator is produced by building %_generator.cpp
$(BUILD_DIR)/%.generator: %_generator.cpp $(GENERATOR_DEPS)
	@echo Building Generator $(filter %_generator.cpp,$^)
	@mkdir -p $(BUILD_DIR)
	@$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -lz -o $@

# By default, %.o/.h are produced by executing %.generator
$(BUILD_DIR)/%.o $(BUILD_DIR)/%.h: $(BUILD_DIR)/%.generator
	@echo Running Generator $<
	@mkdir -p $(BUILD_DIR)
	@$< -g $(notdir $*) -o $(BUILD_DIR) target=$(HL_TARGET)

HL_MODULES = \
	$(BUILD_DIR)/daubechies_x.o \
	$(BUILD_DIR)/haar_x.o \
	$(BUILD_DIR)/inverse_daubechies_x.o \
	$(BUILD_DIR)/inverse_haar_x.o

$(BUILD_DIR)/wavelet_gen.o: wavelet_gen.cpp $(HL_MODULES)
	@$(CXX) $(CXXFLAGS) $(LIBPNG_CXX_FLAGS) -I$(BUILD_DIR) -c $< -o $@

$(BUILD_DIR)/wavelet_gen: $(BUILD_DIR)/wavelet_gen.o 
	@$(CXX) $(CXXFLAGS) $^ $(HL_MODULES) $(PNGFLAGS) -o $@

test: $(BUILD_DIR)/wavelet_gen
	@echo Testing wavelet_gen...
	@$< ../images/gray.png $(BUILD_DIR)

# Don't auto-delete the generators.
.SECONDARY:

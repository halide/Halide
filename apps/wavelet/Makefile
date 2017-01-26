include ../support/Makefile.inc

BIN ?= bin

# If HL_TARGET isn't set, use host
HL_TARGET ?= host

all: $(BIN)/wavelet

clean:
	@rm -rf $(BIN)

# By default, %_exec is produced by building %_generator.cpp
$(BIN)/%_exec: %_generator.cpp $(GENERATOR_DEPS)
	@echo Building Generator $(filter %_generator.cpp,$^)
	@mkdir -p $(BIN)
	@$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) $(LDFLAGS) -o $@

# By default, %.a/.h are produced by executing %_exec
$(BIN)/%.a $(BIN)/%.h: $(BIN)/%_exec
	@echo Running Generator $<
	@mkdir -p $(BIN)
	@$< -g $(notdir $*) -o $(BIN) target=$(HL_TARGET)-no_runtime

$(BIN)/runtime_$(HL_TARGET).a: $(BIN)/haar_x_exec
	@echo Compiling Halide runtime for target $(HL_TARGET)
	@mkdir -p $(BIN)
	@$< -r runtime_$(HL_TARGET) -o $(BIN) target=$(HL_TARGET)

HL_MODULES = \
	$(BIN)/daubechies_x.a \
	$(BIN)/haar_x.a \
	$(BIN)/inverse_daubechies_x.a \
	$(BIN)/inverse_haar_x.a \
	$(BIN)/runtime_$(HL_TARGET).a

$(BIN)/wavelet.a: wavelet.cpp $(HL_MODULES)
	@$(CXX) $(CXXFLAGS) $(IMAGE_IO_CXX_FLAGS) -I$(BIN) -c $< -o $@

$(BIN)/wavelet: $(BIN)/wavelet.a
	@$(CXX) $(CXXFLAGS) $^ $(HL_MODULES) $(IMAGE_IO_LIBS) $(LDFLAGS) -o $@

test: $(BIN)/wavelet
	@echo Testing wavelet...
	@$< ../images/gray.png $(BIN)

# Don't auto-delete the generators.
.SECONDARY:

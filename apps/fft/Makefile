include ../support/Makefile.inc

CXXFLAGS += -g -Wall

.PHONY: clean

ifeq ($(WITH_FFTW),1)
CXXFLAGS += -DWITH_FFTW
LDFLAGS += -lfftw3f
endif

# Use a small stack, to stress out the compiler and act as a canary
# for stack bloat problems. This can be accomplished with a linker
# flag on os x, and testing this on one platform is sufficient.
UNAME = $(shell uname)
ifeq ($(UNAME), Darwin)
	LDFLAGS += -Wl,-stack_size -Wl,0x100000
endif

$(BIN)/%/bench_fft: main.cpp fft.cpp fft.h complex.h funct.h $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

bench_8x8: $(BIN)/$(HL_TARGET)/bench_fft
	$< 8 8 $(<D)

bench_16x16: $(BIN)/$(HL_TARGET)/bench_fft
	$< 16 16 $(<D)

bench_32x32: $(BIN)/$(HL_TARGET)/bench_fft
	$< 32 32 $(<D)

bench_48x48: $(BIN)/$(HL_TARGET)/bench_fft
	$< 48 48 $(<D)

bench_64x64: $(BIN)/$(HL_TARGET)/bench_fft
	$< 64 64 $(<D)

$(GENERATOR_BIN)/fft.generator: fft_generator.cpp fft.cpp fft.h $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(GENERATOR_LDFLAGS) 

# Generate four AOT compiled FFT variants. Forward versions have gain set to 1 / 256.0
$(BIN)/%/fft_forward_r2c.a: $(GENERATOR_BIN)/fft.generator
	@mkdir -p $(@D)
	$^ -g fft -e $(GENERATOR_OUTPUTS) -o $(@D) -f fft_forward_r2c target=$* direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=real output_number_type=complex

$(BIN)/%/fft_inverse_c2r.a: $(GENERATOR_BIN)/fft.generator
	@mkdir -p $(@D)
	$^ -g fft -e $(GENERATOR_OUTPUTS) -o $(@D) -f fft_inverse_c2r target=$* direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=real

$(BIN)/%/fft_forward_c2c.a: $(GENERATOR_BIN)/fft.generator
	@mkdir -p $(@D)
	$^ -g fft -e $(GENERATOR_OUTPUTS) -o $(@D) -f fft_forward_c2c target=$* direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=complex output_number_type=complex

$(BIN)/%/fft_inverse_c2c.a: $(GENERATOR_BIN)/fft.generator
	@mkdir -p $(@D)
	$^ -g fft -e $(GENERATOR_OUTPUTS) -o $(@D) -f fft_inverse_c2c target=$* direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=complex

$(BIN)/%/fft_aot_test: fft_aot_test.cpp $(BIN)/%/fft_forward_r2c.a $(BIN)/%/fft_inverse_c2r.a $(BIN)/%/fft_forward_c2c.a $(BIN)/%/fft_inverse_c2c.a
	@mkdir -p $(@D)
	$(CXX) -I$(BIN)/$* -I$(HALIDE_DISTRIB_PATH)/include/ -std=c++11 $^ -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

clean:
	rm -rf $(BIN)

fft_aot_test: $(BIN)/$(HL_TARGET)/fft_aot_test
	$^

all: fft_aot_test bench_16x16 bench_32x32 bench_48x48 bench_64x64

# Ensure these are run sequentially and not in parallel
test: $(BIN)/$(HL_TARGET)/bench_fft
	$< 8 8 $(<D)
	$< 16 16 $(<D)
	$< 24 24 $(<D)
	$< 32 32 $(<D)
	$< 48 48 $(<D)

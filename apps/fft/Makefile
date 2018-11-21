include ../support/Makefile.inc

CXXFLAGS += -g -Wall
BIN ?= bin

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

$(BIN)/bench_fft: main.cpp fft.cpp fft.h complex.h funct.h $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) main.cpp fft.cpp $(LIB_HALIDE) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

bench_16x16: $(BIN)/bench_fft
	$(BIN)/bench_fft 16 16 $(BIN)/

bench_32x32: $(BIN)/bench_fft
	$(BIN)/bench_fft 32 32 $(BIN)/

bench_48x48: $(BIN)/bench_fft
	$(BIN)/bench_fft 48 48 $(BIN)/

bench_64x64: $(BIN)/bench_fft
	$(BIN)/bench_fft 64 64 $(BIN)/

$(BIN)/fft.generator: fft_generator.cpp fft.cpp fft.h $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# Generate four AOT compiled FFT variants. Forward versions have gain set to 1 / 256.0
$(BIN)/fft_forward_r2c.a: $(BIN)/fft.generator
	@mkdir -p $(@D)
	$^ -g fft -o $(BIN) -f fft_forward_r2c target=$(HL_TARGET) direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=real output_number_type=complex

$(BIN)/fft_inverse_c2r.a: $(BIN)/fft.generator
	@mkdir -p $(@D)
	$^ -g fft -o $(BIN) -f fft_inverse_c2r target=$(HL_TARGET) direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=real

$(BIN)/fft_forward_c2c.a: $(BIN)/fft.generator
	@mkdir -p $(@D)
	$^ -g fft -o $(BIN) -f fft_forward_c2c target=$(HL_TARGET) direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=complex output_number_type=complex

$(BIN)/fft_inverse_c2c.a: $(BIN)/fft.generator
	@mkdir -p $(@D)
	$^ -g fft -o $(BIN) -f fft_inverse_c2c target=$(HL_TARGET) direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=complex

$(BIN)/fft_aot_test: fft_aot_test.cpp $(BIN)/fft_forward_r2c.a $(BIN)/fft_inverse_c2r.a $(BIN)/fft_forward_c2c.a $(BIN)/fft_inverse_c2c.a
	@mkdir -p $(@D)
	$(CXX) -I$(BIN) -I$(HALIDE_DISTRIB_PATH)/include/ -std=c++11 $^ -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

clean:
	rm -rf $(BIN)

fft_aot_test: $(BIN)/fft_aot_test
	$(BIN)/fft_aot_test

all: fft_aot_test bench_16x16 bench_32x32 bench_48x48 bench_64x64

# Ensure these are run sequentially and not in parallel
test: $(BIN)/bench_fft
	$(BIN)/bench_fft 16 16 $(BIN)/
	$(BIN)/bench_fft 32 32 $(BIN)/
	$(BIN)/bench_fft 48 48 $(BIN)/

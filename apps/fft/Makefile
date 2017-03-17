include ../support/Makefile.inc

CXXFLAGS += -g -Wall
BIN ?= bin

.PHONY: clean

ifeq ($(WITH_FFTW),1)
CXXFLAGS += -DWITH_FFTW
LDFLAGS += -lfftw3f
endif

$(BIN)/bench_fft: main.cpp fft.cpp fft.h complex.h funct.h
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) main.cpp fft.cpp $(LIB_HALIDE) -o $@ $(LDFLAGS) $(LLVM_SHARED_LIBS)

bench_16x16: $(BIN)/bench_fft
	$(BIN)/bench_fft 16 16 $(BIN)/

bench_32x32: $(BIN)/bench_fft
	$(BIN)/bench_fft 32 32 $(BIN)/

bench_48x48: $(BIN)/bench_fft
	$(BIN)/bench_fft 48 48 $(BIN)/

bench_64x64: $(BIN)/bench_fft
	$(BIN)/bench_fft 64 64 $(BIN)/

$(BIN)/fft_generator_exec: fft_generator.cpp fft.cpp fft.h $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

# Generate four AOT compiled FFT variants. Forward versions have gain set to 1 / 256.0
$(BIN)/fft_forward_r2c.a: $(BIN)/fft_generator_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN) -f fft_forward_r2c target=$(HL_TARGET) direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=real output_number_type=complex

$(BIN)/fft_inverse_c2r.a: $(BIN)/fft_generator_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN) -f fft_inverse_c2r target=$(HL_TARGET) direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=real

$(BIN)/fft_forward_c2c.a: $(BIN)/fft_generator_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN) -f fft_forward_c2c target=$(HL_TARGET) direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=complex output_number_type=complex

$(BIN)/fft_inverse_c2c.a: $(BIN)/fft_generator_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN) -f fft_inverse_c2c target=$(HL_TARGET) direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=complex

$(BIN)/fft_aot_test: fft_aot_test.cpp $(BIN)/fft_forward_r2c.a $(BIN)/fft_inverse_c2r.a $(BIN)/fft_forward_c2c.a $(BIN)/fft_inverse_c2c.a
	@-mkdir -p $(BIN)
	$(CXX) -I$(BIN) -I$(HALIDE_BIN_PATH)/include/ -std=c++11 $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN)

fft_aot_test: $(BIN)/fft_aot_test
	$(BIN)/fft_aot_test

all: fft_aot_test bench_16x16 bench_32x32 bench_48x48 bench_64x64

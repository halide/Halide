include ../support/Makefile.inc

CXXFLAGS += -g -Wall

.PHONY: clean

ifeq ($(WITH_FFTW),1)
CXXFLAGS += -DWITH_FFTW
LDFLAGS += -lfftw3f
endif

bench_fft: main.cpp fft.cpp fft.h complex.h funct.h
	$(CXX) $(CXXFLAGS) main.cpp fft.cpp $(LIB_HALIDE) -o bench_fft \
	$(LDFLAGS) $(LLVM_SHARED_LIBS)

bench_16x16: bench_fft
	./bench_fft 16 16

bench_32x32: bench_fft
	./bench_fft 32 32

bench_48x48: bench_fft
	./bench_fft 48 48

bench_64x64: bench_fft
	./bench_fft 64 64

# Generate four AOT compiled FFT variants. Forward versions have gain set to 1 / 256.0
generated/fft_forward_r2c.o: fft_generator.cpp fft.cpp fft.h
	$(HALIDE_SRC_PATH)/tools/gengen.sh -c "c++ -I." -l $(LIB_HALIDE) -e o,h -o generated -s fft_generator.cpp -s fft.cpp -f fft_forward_r2c target=host direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=real output_number_type=complex
generated/fft_inverse_c2r.o: fft_generator.cpp fft.cpp fft.h
	$(HALIDE_SRC_PATH)/tools/gengen.sh -c "c++ -I." -l $(LIB_HALIDE) -e o,h -o generated -s fft_generator.cpp -s fft.cpp -f fft_inverse_c2r target=host direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=real
generated/fft_forward_c2c.o: fft_generator.cpp fft.cpp fft.h
	$(HALIDE_SRC_PATH)/tools/gengen.sh -c "c++ -I." -l $(LIB_HALIDE) -e o,h -o generated -s fft_generator.cpp -s fft.cpp -f fft_forward_c2c target=host direction=samples_to_frequency size0=16 size1=16 gain=0.00390625 input_number_type=complex output_number_type=complex
generated/fft_inverse_c2c.o: fft_generator.cpp fft.cpp fft.h
	$(HALIDE_SRC_PATH)/tools/gengen.sh -c "c++ -I." -l $(LIB_HALIDE) -e o,h -o generated -s fft_generator.cpp -s fft.cpp -f fft_inverse_c2c target=host direction=frequency_to_samples size0=16 size1=16 input_number_type=complex output_number_type=complex

fft_aot_test: fft_aot_test.cpp fft.cpp fft_generator.cpp fft.h generated/fft_forward_r2c.o generated/fft_inverse_c2r.o generated/fft_forward_c2c.o generated/fft_inverse_c2c.o
	$(CXX) fft_aot_test.cpp generated/fft_forward_r2c.o generated/fft_inverse_c2r.o generated/fft_forward_c2c.o generated/fft_inverse_c2c.o -o fft_aot_test

clean:
	rm -f bench_fft c2c.html r2c.html c2r.html

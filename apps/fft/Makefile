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

clean:
	rm -f bench_fft c2c.html r2c.html c2r.html

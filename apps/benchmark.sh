#!/bin/bash

echo "Workload   Halide    Reference"

# Blur
cd blur
make -s test
./test
cd ../

# Unsharp
cd unsharp
make -s unsharp
./unsharp
cd ../

# Bilateral grid
cd bilateral_grid
make -s filter
./filter ../images/gray.png out.png 0.1 10
cd reference_cpu
make -s truncated_kernel_bf
./truncated_kernel_bf ../../images/rgb.ppm out.pgm 10 0.1
cd ../../

# Harris
cd harris
make -s filter
./filter ../images/rgb.png
cd ../

# interpolate
cd interpolate
make -s interpolate
./interpolate ../images/rgba.png out.png
cd ../

# Local laplacian
cd local_laplacian
make -s process
./process ../images/rgb.png 8 1 1 10 out.png
cd ../

# Camera pipe
cd camera_pipe
make -s process
./process ../images/bayer_raw.png 3700 2.0 50 5 out.png
cd ../

# GEMM

# FFT
cd fft
WITH_FFTW=1 make -s bench_fft
./bench_fft 16 16
./bench_fft 32 32
./bench_fft 48 48
./bench_fft 64 64
cd ../

# Conv/VGG

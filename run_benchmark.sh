#!/bin/bash

BIN="./bin"
IMAGES="../images"
BENCHMARK_DATA="../benchmark_data"
BENCHMARK_PLOT="../benchmark_plot"

echo "Run apps auto-scheduler benchmarks"

make -j4

mkdir -p ./apps/benchmark_data
mkdir -p ./apps/benchmark_plot

# Bilateral grid
echo "Run bilateral grid"
cd ./apps/bilateral_grid
make clean
HL_TARGET=host-profile make -j4 all
$BIN/filter $IMAGES/gray.png $BIN/out.png 0.1 10 &> $BENCHMARK_DATA/bilateral_grid.txt
cd ../..

# Blur
echo "Run blur"
cd ./apps/blur
make clean
HL_TARGET=host-profile make -j4 all
$BIN/test &> $BENCHMARK_DATA/blur.txt
cd ../..

# Camera pipe
echo "Run camera pipe"
cd ./apps/camera_pipe
make clean
HL_AUTOSCHEDULE_GRAPHVIZ=$BENCHMARK_PLOT/camera_pipe.dot HL_TARGET=host-profile make -j4 all
$BIN/process $IMAGES/bayer_raw.png 3700 2.0 50 5 $@ $BIN/h_auto.png $BIN/fcam_c.png $BIN/fcam_arm.png &> $BENCHMARK_DATA/camera_pipe.txt
cd ../..

# Convolution layer
echo "Run conv layer"
cd ./apps/conv_layer
make clean
HL_AUTOSCHEDULE_GRAPHVIZ=$BENCHMARK_PLOT/conv_layer.dot HL_TARGET=host-profile make -j4 all
$BIN/process &> $BENCHMARK_DATA/conv_layer.txt
cd ../..

# Lens blur
echo "Run lens blur"
cd ./apps/lens_blur
make clean
HL_AUTOSCHEDULE_GRAPHVIZ=$BENCHMARK_PLOT/lens_blur.dot HL_TARGET=host-profile make -j4 all
$BIN/process $IMAGES/rgb_small.png 32 13 0.5 32 3 $BIN/out.png &> $BENCHMARK_DATA/lens_blur.txt
cd ../..

# Local laplacian
echo "Run local laplacian"
cd ./apps/local_laplacian
make clean
HL_AUTOSCHEDULE_GRAPHVIZ=$BENCHMARK_PLOT/local_laplacian.dot HL_TARGET=host-profile make -j4 all
$BIN/process $IMAGES/rgb.png 8 1 1 10 $BIN/out.png &> $BENCHMARK_DATA/local_laplacian.txt
cd ../..

# NL means
echo "Run nl means"
cd ./apps/nl_means
make clean
HL_AUTOSCHEDULE_GRAPHVIZ=$BENCHMARK_PLOT/nl_means.dot HL_TARGET=host-profile make -j4 all
$BIN/process $IMAGES/rgb.png 7 7 0.12 10 $BIN/out.png &> $BENCHMARK_DATA/nl_means.txt
cd ../..

# Stencil chain
echo "Run stencil chain"
cd ./apps/stencil_chain
make clean
HL_AUTOSCHEDULE_GRAPHVIZ=$BENCHMARK_PLOT/stencil_chain.dot HL_TARGET=host-profile make -j4 all
$BIN/process $IMAGES/rgb.png 10 $BIN/out.png &> $BENCHMARK_DATA/stencil_chain.txt
cd ../../

# Run dot on all the .dot files
for F in apps/benchmark_plot/*.dot; do dot -Tpng -o ${F/.dot/_groups.png} $F ; done

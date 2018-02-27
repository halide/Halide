#!/bin/bash

BIN="./bin"
IMAGES="../images"
BENCHMARK_DATA="../benchmark_data"
BENCHMARK_PLOT="../benchmark_plot"

balances=(1 40 100)

echo "Run apps auto-scheduler benchmarks"

#rm -rf ./apps/benchmark_data
#rm -rf ./apps/benchmark_plot
#rm -rf ./apps/benchmark_csv

#mkdir -p ./apps/benchmark_data
#mkdir -p ./apps/benchmark_plot
#mkdir -p ./apps/benchmark_csv

# Bilateral grid
echo "Run bilateral grid"
cd ./apps/bilateral_grid
INLINE_MAX=2
FAST_MEM=4
TOTAL_MAX=7
for balance in "${balances[@]}"
do
	for fuse_max in `seq 0 $TOTAL_MAX`
	do
		echo "...balance: $balance, total fusion max: $fuse_max"
		make clean
		HL_MACHINE_PARAMS=16,16777216,$balance,-1,-1,$fuse_max HL_TARGET=host make -j4 all >> $BENCHMARK_DATA/bilateral_grid.txt 2>&1
		$BIN/filter $IMAGES/gray.png $BIN/out.png 0.1 10 >> $BENCHMARK_DATA/bilateral_grid.txt 2>&1
	done
done
cd ../..

# Camera pipe
echo "Run camera pipe"
cd ./apps/camera_pipe
INLINE_MAX=9
FAST_MEM=15
TOTAL_MAX=27
for balance in "${balances[@]}"
do
	for fuse_max in `seq 0 $TOTAL_MAX`
	do
		make clean
		HL_MACHINE_PARAMS=16,16777216,$balance,-1,-1,$fuse_max HL_TARGET=host make -j4 all >> $BENCHMARK_DATA/camera_pipe.txt 2>&1
		$BIN/process $IMAGES/bayer_raw.png 3700 2.0 50 5 $@ $BIN/h_auto.png $BIN/fcam_c.png $BIN/fcam_arm.png >> $BENCHMARK_DATA/camera_pipe.txt 2>&1
	done
done
cd ../..

# Convolution layer
echo "Run conv layer"
cd ./apps/conv_layer
INLINE_MAX=0
FAST_MEM=1
TOTAL_MAX=2
for balance in "${balances[@]}"
do
	for fuse_max in `seq 0 $TOTAL_MAX`
	do
		echo "...balance: $balance, total fusion max: $fuse_max"
		make clean
		HL_MACHINE_PARAMS=16,16777216,$balance,-1,-1,$fuse_max HL_TARGET=host make -j4 all >> $BENCHMARK_DATA/conv_layer.txt 2>&1
		$BIN/process >> $BENCHMARK_DATA/conv_layer.txt 2>&1
	done
done
cd ../..

# Lens blur
echo "Run lens blur"
cd ./apps/lens_blur
INLINE_MAX=15
FAST_MEM=23
TOTAL_MAX=54
for balance in "${balances[@]}"
do
	for fuse_max in `seq 0 $TOTAL_MAX`
	do
		echo "...balance: $balance, total fusion max: $fuse_max"
		make clean
		HL_MACHINE_PARAMS=16,16777216,$balance,-1,-1,$fuse_max HL_TARGET=host make -j4 all >> $BENCHMARK_DATA/lens_blur.txt 2>&1
		$BIN/process $IMAGES/rgb_small.png 32 13 0.5 32 3 $BIN/out.png >> $BENCHMARK_DATA/lens_blur.txt 2>&1
	done
done
cd ../..

# Local laplacian
echo "Run local laplacian"
cd ./apps/local_laplacian
INLINE_MAX=20
FAST_MEM=11
TOTAL_MAX=62
for balance in "${balances[@]}"
do
	for fuse_max in `seq 0 $TOTAL_MAX`
	do
		echo "...balance: $balance, total fusion max: $fuse_max"
		make clean
		HL_MACHINE_PARAMS=16,16777216,$balance,-1,-1,$fuse_max HL_TARGET=host make -j4 all >> $BENCHMARK_DATA/local_laplacian.txt 2>&1
		$BIN/process $IMAGES/rgb.png 8 1 1 10 $BIN/out.png >> $BENCHMARK_DATA/local_laplacian.txt 2>&1
	done
done

# NL means
echo "Run nl means"
cd ./apps/nl_means
INLINE_MAX=2
FAST_MEM=6
TOTAL_MAX=9
for balance in "${balances[@]}"
do
	for fuse_max in `seq 0 $TOTAL_MAX`
	do
		echo "...balance: $balance, total fusion max: $fuse_max"
		make clean
		HL_MACHINE_PARAMS=16,16777216,$balance,-1,-1,$fuse_max HL_TARGET=host make -j4 all >> $BENCHMARK_DATA/nl_means.txt 2>&1
		$BIN/process $IMAGES/rgb.png 7 7 0.12 10 $BIN/out.png >> $BENCHMARK_DATA/nl_means.txt 2>&1
	done
done
cd ../..

# Stencil chain
echo "Run stencil chain"
cd ./apps/stencil_chain
INLINE_MAX=0
FAST_MEM=7
TOTAL_MAX=8
for balance in "${balances[@]}"
do
	for fuse_max in `seq 0 $TOTAL_MAX`
	do
		echo "...balance: $balance, total fusion max: $fuse_max"
		make clean
		HL_MACHINE_PARAMS=16,16777216,$balance,-1,-1,$fuse_max HL_TARGET=host make -j4 all >> $BENCHMARK_DATA/stencil_chain.txt 2>&1
		$BIN/process $IMAGES/rgb.png 10 $BIN/out.png >> $BENCHMARK_DATA/stencil_chain.txt 2>&1
	done
done
cd ../../


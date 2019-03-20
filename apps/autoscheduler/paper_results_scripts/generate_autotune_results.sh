HALIDE=$(dirname $0)/../../..

echo "Using Halide in " $HALIDE

export CXX="ccache c++"

export HL_MACHINE_PARAMS=16,24000000,160
export HL_PERMIT_FAILED_UNROLL=1
export HL_WEIGHTS_DIR=${PWD}/${HALIDE}/apps/autoscheduler/weights
export HL_TARGET=x86-64-avx2

#export HL_BEAM_SIZE=1
#export HL_NUM_PASSES=1
export HL_BEAM_SIZE=32
export HL_NUM_PASSES=5
export HL_RANDOM_DROPOUT=100

APPS="resnet_50_blockwise bgu bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate_generator conv_layer mat_mul_generator iir_blur_generator"

while [ 1 ]; do
    for app in $APPS; do
        SECONDS=0
        # 15 mins of autotuning per app, round robin
        while [[ SECONDS -lt 900 ]]; do
            make -C ${HALIDE}/apps/${app} autotune
        done
    done
done

autoscheduler="$1"

HALIDE=$(dirname $0)/../../..
echo "Using Halide in " $HALIDE

# export CXX="ccache c++"
export CXX="c++"

# Best single set of params for master on the benchmarking machine, found with grid search on the runtime pipelines
# There are already baked into src/AutoSchedule.cpp as the default
# export HL_MACHINE_PARAMS=32,24000000,160

export HL_PERMIT_FAILED_UNROLL=1
export HL_WEIGHTS_DIR=${HALIDE}/apps/autoscheduler/weights
export HL_TARGET="host" # x86-64-avx2

# no random dropout
export HL_RANDOM_DROPOUT=100

if [ "$autoscheduler" == "greedy" ]; then
    cp ../AutoSchedule-master.cpp ../AutoSchedule.cpp

    # greedy
    export HL_BEAM_SIZE=1
    export HL_NUM_PASSES=1
    results="greedy"
elif [ "$autoscheduler" == "beam" ]; then
    cp ../AutoSchedule-master.cpp ../AutoSchedule.cpp

    # beam search
    export HL_BEAM_SIZE=32
    export HL_NUM_PASSES=5
    results="beam"
elif [ "$autoscheduler" == "mcts" ]; then
    cp ../AutoSchedule-mcts.cpp ../AutoSchedule.cpp

    # mcts
    export MCTS_MAX_MILLIS=0
    export MCTS_MAX_ITERATIONS=10
    export MCTS_SIMULATION_DEPTH=10
    results="mcts"
else
    echo "usage: $0 [greedy|beam|mcts]"
    exit 1
fi

# ablation where we restrict to old space
# export HL_NO_SUBTILING=1

# ablation where instead of coarse to fine, we just enlarge the beam
# export HL_BEAM_SIZE=160
# export HL_NUM_PASSES=1

# Build the autoscheduler
cd ..
make bin/libauto_schedule.so
if [ $? -ne 0 ]; then
    exit 1
fi
cd -

# APPS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate_generator conv_layer mat_mul_generator iir_blur_generator resnet_50_blockwise bgu"

APPS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate conv_layer iir_blur bgu" # Missing mat_mul_generator and resnet_50_blockwise

APPS="bilateral_grid" # Missing mat_mul_generator and resnet_50_blockwise

# Uncomment when there's a change that wouldn't be picked up by the Makefiles (e.g. new weights)
for app in ${APPS}; do make -C ${HALIDE}/apps/${app} clean; done

# make -j -C ${HALIDE}/apps/bilateral_grid bin/filter
# make -j -C ${HALIDE}/apps/local_laplacian bin/process
# make -j -C ${HALIDE}/apps/nl_means bin/process
# make -j -C ${HALIDE}/apps/lens_blur bin/process 
# make -j -C ${HALIDE}/apps/camera_pipe bin/process 
# make -j -C ${HALIDE}/apps/stencil_chain bin/process 
# make -j -C ${HALIDE}/apps/harris bin/filter
# make -j -C ${HALIDE}/apps/hist bin/filter
# make -j -C ${HALIDE}/apps/max_filter bin/filter
# make -j -C ${HALIDE}/apps/unsharp bin/filter
# make -j -C ${HALIDE}/apps/interpolate_generator bin/filter
# make -j -C ${HALIDE}/apps/conv_layer bin/process
# make -j -C ${HALIDE}/apps/mat_mul_generator bin/filter
# make -j -C ${HALIDE}/apps/iir_blur_generator bin/process
# make -j -C ${HALIDE}/apps/resnet_50_blockwise test &> $results/resnet_50_blockwise.txt
# make -j -C ${HALIDE}/apps/bgu bin/process

for app in ${APPS}; do make -C ${HALIDE}/apps/${app} build; done

mkdir $results 2>/dev/null

# benchmark everything
for app in ${APPS}; do make -C ${HALIDE}/apps/${app} test &> $results/$app.txt; done


autoscheduler="$1"
if [ "$#" -lt 2 ] || [ "$2" != "--improved" ]; then
    improved=0
else
    improved=1
fi

export AUTOTUNE_AUTOSCHEDULER="$autoscheduler"

HALIDE=$(dirname $0)/../../..

echo "Using Halide in " $HALIDE

# export CXX="ccache c++"
export CXX="c++"

export HL_MACHINE_PARAMS=32,24000000,160
export HL_PERMIT_FAILED_UNROLL=1
if [ "$improved" -ne 1 ]; then
    export HL_WEIGHTS_DIR="$PWD/../baseline.weights"
else
    echo Using improved weights
    export HL_WEIGHTS_DIR="$PWD/../improved.weights"
fi
# export HL_TARGET=x86-64-avx2
export HL_TARGET="host"

export HL_RANDOM_DROPOUT=100

#export HL_BEAM_SIZE=1
#export HL_NUM_PASSES=1
# export HL_BEAM_SIZE=32
# export HL_NUM_PASSES=5

if [ "$autoscheduler" == "greedy" ]; then
    if diff ../AutoSchedule-master.cpp ../AutoSchedule.cpp > /dev/null ; then
        echo "No need to copy AutoSchedule-master.cpp"
    else
        cp ../AutoSchedule-master.cpp ../AutoSchedule.cpp
    fi

    # greedy
    export HL_BEAM_SIZE=1
    export HL_NUM_PASSES=1
elif [ "$autoscheduler" == "beam" ]; then
    if diff ../AutoSchedule-master.cpp ../AutoSchedule.cpp > /dev/null ; then
        echo "No need to copy AutoSchedule-master.cpp"
    else
        cp ../AutoSchedule-master.cpp ../AutoSchedule.cpp
    fi

    # beam search
    export HL_BEAM_SIZE=32
    export HL_NUM_PASSES=5
elif [ "$autoscheduler" == "mcts" ]; then
    if diff ../AutoSchedule-mcts.cpp ../AutoSchedule.cpp > /dev/null ; then
        echo "No need to copy AutoSchedule-mcts.cpp"
    else
        cp ../AutoSchedule-mcts.cpp ../AutoSchedule.cpp
    fi

    # mcts
    export HL_NUM_PASSES=1
    export MCTS_MAX_MILLIS=0
    export MCTS_MAX_ITERATIONS=16
    export MCTS_SIMULATION_DEPTH=5
    export MCTS_DEPTH=16
elif [ "$autoscheduler" == "master" ]; then
    # master
    export HL_BEAM_SIZE=1
    export HL_NUM_PASSES=1
    export HL_CUSTOM_AUTOSCHEDULER=""
else
    echo "usage: $0 [master|greedy|beam|mcts]"
    exit 1
fi

if [ "$RETRAIN" != "true" ] && [ "$RETRAIN" != "false" ]; then
    echo You must set RETRAIN env variable to \"true\" or \"false\"
    exit 1
fi

if [ "$autoscheduler" != "master" ]; then
    echo Building autotune and retraining libraries
    cd ..
    make autotune_deps
    if [ $? -ne 0 ]; then
        echo "Failed to build autoscheduler and retraining binaries"
        exit 1
    fi
    cd -
fi

# APPS="resnet_50_blockwise bgu bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate_generator conv_layer mat_mul_generator iir_blur_generator"

APPS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate conv_layer iir_blur bgu mat_mul" # Missing resnet_50_blockwise

APPS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate conv_layer iir_blur bgu mat_mul" # Missing resnet_50_blockwise

for app in $APPS; do
    echo "$app ($autoscheduler) (retrain=$RETRAIN)" >> autoprogress

    make -C ${HALIDE}/apps/${app} clean

    # Build app
    if [ "$app" != "iir_blur" ] && [ "$app" != "harris" ] && [ "$app" != "unsharp" ] ; then
        make -C ${HALIDE}/apps/${app} build
    else
        make -C ${HALIDE}/apps/${app} all
    fi

    if [ $? -ne 0 ]; then
        echo "Failed to build $app"
        echo "Failed to build $app ($autoscheduler) (retrain=$RETRAIN)" >> autoerrors
        # exit 1
    fi

    # Move old samples away
    if [ -d "${HALIDE}/apps/${app}/samples" ]; then
        i=0
        while [ -d "${HALIDE}/apps/${app}/old_samples_$i" ]; do
            i=$((i+1))
        done
        mv "${HALIDE}/apps/${app}/samples" "${HALIDE}/apps/${app}/old_samples_$i"
    fi

    if [ "$RETRAIN" == "true" ]; then
        MAX_SECONDS=10800 # 3 hours
    else
        MAX_SECONDS=600 # 10 minutes
    fi

    first_autotune="true"
    SECONDS=0

    while [[ SECONDS -lt $MAX_SECONDS ]]; do
        # Use the correct weights
        if [ "$first_autotune" == "true" ] || [ "$RETRAIN" == "false" ]; then
            if [ "$improved" -ne 1 ]; then
                export HL_WEIGHTS_DIR="$PWD/../baseline.weights"
            else
                export HL_WEIGHTS_DIR="$PWD/../improved.weights"
            fi
            first_autotune="false"
        else
            export HL_WEIGHTS_DIR="$PWD/../../${app}/samples/updated.weights"
        fi

        # Run the autotuning script
        make -C ${HALIDE}/apps/${app} autotune

        # Check if the program autotuned correctly
        if [ $? -ne 0 ]; then
            echo "Failed to retrain $app"
            echo "Failed to retrain $app ($autoscheduler) (retrain=$RETRAIN)" >> autoerrors
            # exit 1
        fi
    done

    TOTAL_SECONDS=$SECONDS

    echo "$app took $TOTAL_SECONDS seconds (max_seconds=$MAX_SECONDS)"
    echo "$app took $TOTAL_SECONDS seconds ($autoscheduler) (retrain=$RETRAIN) (max_seconds=$MAX_SECONDS)" >> autostats
done

./show_autotune_results.sh "$autoscheduler"

# Store the newly created samples in directories so we can find them later
for app in $APPS; do
    echo "Moving samples of ${app}"

    i=0
    while [ -d "${HALIDE}/apps/${app}/samples_${autoscheduler}_$i" ]; do
        i=$((i+1))
    done
    mv "${HALIDE}/apps/${app}/samples" "${HALIDE}/apps/${app}/samples_${autoscheduler}_$i"
done


autoscheduler="$1"
if [ "$#" -lt 2 ]; then
    weights=0
elif  [ "$2" == "--improved" ]; then
    weights=1
elif  [ "$2" == "--value_func" ]; then
    weights=2
else
    echo "Invalid command line option!"
fi

HALIDE=$(dirname $0)/../../..
echo "Using Halide in " $HALIDE

# export CXX="ccache c++"
export CXX="c++"

# Best single set of params for master on the benchmarking machine, found with grid search on the runtime pipelines
# There are already baked into src/AutoSchedule.cpp as the default
# export HL_MACHINE_PARAMS=32,24000000,160

export HL_PERMIT_FAILED_UNROLL=1
# export HL_WEIGHTS_DIR=${HALIDE}/apps/autoscheduler/weights
if [ "$weights" -lt 1 ]; then
    export HL_WEIGHTS_DIR="$PWD/../baseline.weights"
elif [ "$weights" -eq 1 ]; then
    export HL_WEIGHTS_DIR="$PWD/../improved.weights"
elif [ "$weights" -eq 2 ]; then
    echo "Use $PWD/../value_func.weights."
    export HL_WEIGHTS_DIR="$PWD/../value_func.weights"
else
    echo "Invalid weights option!"
fi
export HL_TARGET="host" # x86-64-avx2

# no random dropout
export HL_RANDOM_DROPOUT=100


unset HL_RANDOM
if [ "$autoscheduler" == "random" ]; then
    if diff ../AutoSchedule-master.cpp ../AutoSchedule.cpp > /dev/null ; then
        echo "No need to copy AutoSchedule-master.cpp"
    else
        cp ../AutoSchedule-master.cpp ../AutoSchedule.cpp
    fi

    export HL_RANDOM=1
    # random
    export HL_BEAM_SIZE=1
    export HL_NUM_PASSES=1
    results="random"
elif [ "$autoscheduler" == "greedy" ]; then
    if diff ../AutoSchedule-master.cpp ../AutoSchedule.cpp > /dev/null ; then
        echo "No need to copy AutoSchedule-master.cpp"
    else
        cp ../AutoSchedule-master.cpp ../AutoSchedule.cpp
    fi

    # greedy
    export HL_BEAM_SIZE=1
    export HL_NUM_PASSES=1
    results="greedy"
elif [ "$autoscheduler" == "beam" ]; then
    if diff ../AutoSchedule-master.cpp ../AutoSchedule.cpp > /dev/null ; then
        echo "No need to copy AutoSchedule-master.cpp"
    else
        cp ../AutoSchedule-master.cpp ../AutoSchedule.cpp
    fi

    # beam search
    export HL_BEAM_SIZE=32
    export HL_NUM_PASSES=5
    results="beam"
elif [ "$autoscheduler" == "mcts" ]; then
    if diff ../AutoSchedule-mcts.cpp ../AutoSchedule.cpp > /dev/null ; then
        echo "No need to copy AutoSchedule-master.cpp"
    else
        cp ../AutoSchedule-mcts.cpp ../AutoSchedule.cpp
    fi

    # mcts
    export HL_NUM_PASSES=16
    export MCTS_NUM_RANDOM_TREES=15
    export HL_SEED=13
    export MCTS_MAX_MILLIS=30000
    export MCTS_MAX_ITERATIONS=10000
    results="mcts"
elif [ "$autoscheduler" == "master" ]; then
    # master
    export HL_BEAM_SIZE=1
    export HL_NUM_PASSES=1
    export HL_CUSTOM_AUTOSCHEDULER=""
    results="master"
else
    echo "usage: $0 [master|greedy|beam|mcts]"
    exit 1
fi

# ablation where we restrict to old space
# export HL_NO_SUBTILING=1

# ablation where instead of coarse to fine, we just enlarge the beam
# export HL_BEAM_SIZE=160
# export HL_NUM_PASSES=1

# Build the autoscheduler
if [ "$autoscheduler" != "master" ]; then
    cd ..
    make bin/libauto_schedule.so
    if [ $? -ne 0 ]; then
        echo "Failed to build autoscheduler library"
        exit 1
    fi
    cd -
fi

#APPS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate_generator conv_layer mat_mul_generator iir_blur_generator resnet_50_blockwise bgu"
 
APPS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate conv_layer mat_mul iir_blur bgu" # resnet_50_blockwise is handled by a special case at the end


benchmark_resnet="true"
RANDOM_DUR=600

if [ "$APPS" != "" ]; then
    # Uncomment when there's a change that wouldn't be picked up by the Makefiles (e.g. new weights)
    for app in ${APPS}; do make -C ${HALIDE}/apps/${app} clean; done


    mkdir $results 2>/dev/null
    for app in ${APPS}; do
        echo "building $app (autoscheduler == $autoscheduler)" >> progress
        if [ "$autoscheduler" == "random" ]; then
            echo > $results/$app.randtime.txt
            start=$SECONDS
            if [ "$app" != "iir_blur" ] && [ "$app" != "harris" ] && [ "$app" != "unsharp" ] ; then
                make -C ${HALIDE}/apps/${app} build > $results/$app.log
            else
                make -C ${HALIDE}/apps/${app} all > $results/$app.log
            fi

            grep 'JENNY_EVALTIME' $results/$app.log > $results/$app.evaltime.txt 
            grep 'JENNY_MINCOST' $results/$app.log > $results/$app.mincost.txt 
            duration=$(( SECONDS - start ))
            total_duration=$duration
            make -C ${HALIDE}/apps/${app} test &>> $results/$app.randtime.txt
            while [ $total_duration -lt $RANDOM_DUR ]; do
                make -C ${HALIDE}/apps/${app} clean
                start=$SECONDS
                if [ "$app" != "iir_blur" ] && [ "$app" != "harris" ] && [ "$app" != "unsharp" ] ; then
                    make -C ${HALIDE}/apps/${app} build > $results/$app.log
                else
                    make -C ${HALIDE}/apps/${app} all > $results/$app.log
                fi

                grep 'JENNY_EVALTIME' $results/$app.log >> $results/$app.evaltime.txt 
                grep 'JENNY_MINCOST' $results/$app.log >> $results/$app.mincost.txt 
               
                duration=$(( SECONDS - start ))
                total_duration=$((total_duration + duration))
                echo "TOTAL_DUR: $total_duration"
                echo "$app $total_duration" > $results/$app.runtime.txt

                make -C ${HALIDE}/apps/${app} test &>> $results/$app.randtime.txt
            done
        else
            start=$SECONDS
            if [ "$app" != "iir_blur" ] && [ "$app" != "harris" ] && [ "$app" != "unsharp" ] ; then
                make -C ${HALIDE}/apps/${app} build > $results/$app.log
            else
                make -C ${HALIDE}/apps/${app} all > $results/$app.log
            fi

            grep 'JENNY_EVALTIME' $results/$app.log > $results/$app.evaltime.txt 
            grep 'JENNY_MINCOST' $results/$app.log > $results/$app.mincost.txt 

            duration=$(( SECONDS - start ))
            echo "$app $duration" > $results/$app.runtime.txt
        fi

        if [ $? -ne 0 ]; then
            echo "Failed to build $app"
            echo "Failed to build $app (autoscheduler == $autoscheduler)" >> errors
            # exit 1
        fi
    done


    # benchmark everything
    for app in ${APPS}; do
        echo "running $app (autoscheduler == $autoscheduler)" >> progress

        if [ "$RL_FIRST" != "false" ]; then
            echo > $results/$app.txt
        fi

        make -C ${HALIDE}/apps/${app} test &>> $results/$app.txt

        printf "\n\nRL_END_OF_RUN %d\n\n" $HL_SEED >> $results/$app.txt

        if [ $? -ne 0 ]; then
            echo "Failed to benchmark $app"
            echo "Failed to benchmark $app (autoscheduler == $autoscheduler)" >> errors
            # exit 1
        fi
    done
fi

# Special case for resnet_50_blockwise
if [ "$benchmark_resnet" == "true" ]; then
    app="resnet_50_blockwise"

    echo "building $app (autoscheduler == $autoscheduler)" >> progress

    make -C ${HALIDE}/apps/${app} clean

    if [ "$autoscheduler" != "mcts" ]; then
        cores=$(nproc)
    else
        cores=2
    fi

    if [ "$autoscheduler" == "random" ]; then
        echo > $results/$app.randtime.txt
        start=$SECONDS
        make -C ${HALIDE}/apps/${app} all > $results/$app.log

        grep 'JENNY_EVALTIME' $results/$app.log > $results/$app.evaltime.txt 
        grep 'JENNY_MINCOST' $results/$app.log > $results/$app.mincost.txt 

        duration=$(( SECONDS - start ))
        total_duration=$duration
        #make -C ${HALIDE}/apps/${app} test_manual &>> $results/$app.randtime.txt
        make -C ${HALIDE}/apps/${app} test_auto_schedule &>> $results/$app.randtime.txt

        while [ $total_duration -lt $RANDOM_DUR ]; do
            make -C ${HALIDE}/apps/${app} clean
            start=$SECONDS
            make -C ${HALIDE}/apps/${app} all > $results/$app.log

            grep 'JENNY_EVALTIME' $results/$app.log >> $results/$app.evaltime.txt 
            grep 'JENNY_MINCOST' $results/$app.log >> $results/$app.mincost.txt 
           
            duration=$(( SECONDS - start ))
            total_duration=$((total_duration + duration))
            echo "TOTAL_DUR: $total_duration"
            echo "$app $total_duration" > $results/$app.runtime.txt

            #make -C ${HALIDE}/apps/${app} test_manual &>> $results/$app.randtime.txt
            make -C ${HALIDE}/apps/${app} test_auto_schedule &>> $results/$app.randtime.txt
        done
    else 
        start=$SECONDS
        make -C ${HALIDE}/apps/${app} all -j${cores} > $results/$app.log

        grep 'JENNY_EVALTIME' $results/$app.log > $results/$app.evaltime.txt 
        grep 'JENNY_MINCOST' $results/$app.log > $results/$app.mincost.txt 

        duration=$(( SECONDS - start ))
        echo "$app $duration" > $results/$app.runtime.txt
    fi

    echo "running $app (autoscheduler == $autoscheduler)" >> progress

    if [ "$RL_FIRST" != "false" ]; then
        echo > $results/$app.txt
    fi

    if [ "$autoscheduler" == "greedy" ]; then
        make -C ${HALIDE}/apps/${app} test_manual &>> $results/$app.txt
        make -C ${HALIDE}/apps/${app} test_auto_schedule &>> $results/$app.txt
    elif [ "$autoscheduler" == "master" ]; then
        make -C ${HALIDE}/apps/${app} test_classic_auto_schedule &>> $results/$app.txt
    else
        make -C ${HALIDE}/apps/${app} test_auto_schedule &>> $results/$app.txt
    fi

    printf "\n\nRL_END_OF_RUN %d\n\n" $HL_SEED >> $results/$app.txt
fi

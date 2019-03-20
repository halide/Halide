HALIDE=$(dirname $0)/../../..
echo "Using Halide in " $HALIDE

export CXX="ccache c++"

# Best single set of params for master on the benchmarking machine, found with grid search on the runtime pipelines
# There are already baked into src/AutoSchedule.cpp as the default
# export HL_MACHINE_PARAMS=32,24000000,160

# The 6-config setting from the Mullapudi paper doubled and halved the
# cache size, and doubled the balance parameter. The meaning of these
# parameters has changed somewhat since then though. The memory cost
# was a step function, but now it's linear up to some maximum. We have
# set it to the size of L3 on the benchmarking machine (24MB), because
# that gave best results on the random pipelines. It doesn't make
# sense to increase it further, so we'll only test reductions of
# it. The slope of the linear part of the cost is controlled by the
# ratio of the cache parameter and the balance parameter. So when we
# double the cache parameter we really also need to double the balance
# parameter if we wish to hold the slope fixed. We therefore use the
# following nine configurations to be sure we're being fair to prior
# work:

for config in 32,24000000,160 32,12000000,80 32,6000000,40 32,24000000,80 32,12000000,40 32,6000000,20 32,24000000,320 32,12000000,160 32,6000000,80 ; do
    export HL_MACHINE_PARAMS=${config}

    export HL_PERMIT_FAILED_UNROLL=1
    export HL_WEIGHTS_DIR=${PWD}/${HALIDE}/apps/autoscheduler/weights
    export HL_TARGET=x86-64-avx2
    
    export HL_RANDOM_DROPOUT=100


    # We won't be using the numbers for the new autoscheduler in this
    # experiment, so just use the fastest settings.  greedy
    export HL_BEAM_SIZE=1
    export HL_NUM_PASSES=1
    
    # beam search
    #export HL_BEAM_SIZE=32
    #export HL_NUM_PASSES=5
    
    # restrict to old space
    #export HL_NO_SUBTILING=1

    APPS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate_generator conv_layer mat_mul_generator iir_blur_generator resnet_50_blockwise bgu"
    
    # Uncomment when there's a change that wouldn't be picked up by the Makefiles (e.g. new weights)
    for app in ${APPS}; do make -C ${HALIDE}/apps/${app} clean; done

    for app in ${APPS}; do
        # The apps sadly do not use consistent names for their test binary, but they're all either 'filter' or 'process'
        if [ -f ${HALIDE}/apps/${app}/filter.cpp ]; then
            make -j32 -C ${HALIDE}/apps/${app} bin/filter 2>stderr_${app}.txt >stdout_${app}.txt &
        else
            make -j32 -C ${HALIDE}/apps/${app} bin/process 2>stderr_${app}.txt >stdout_${app}.txt &
        fi
    done
    make -C ${HALIDE}/apps/resnet_50_blockwise bin/pytorch_weights/ok > /dev/null 2> /dev/null
    wait
    
    # benchmark everything
    echo "Benchmarks for config: " $config 
    for app in ${APPS}; do make -C ${HALIDE}/apps/${app} test; done
done

# Report results
for app in ${APPS}; do
    echo $app
    egrep 'Entering.*bilateral_grid|Classic' out.txt | grep 'bilateral_grid' -A1 | grep 'Classic' | cut -d' ' -f4 | sort -n | head -n1
done

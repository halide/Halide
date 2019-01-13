HALIDE=$(dirname $0)/../../..
echo "Using Halide in " $HALIDE

export CXX="ccache ${CXX}"

# Best single set of params for master on the benchmarking machine, found with grid search on the runtime pipelines
# There are already baked into src/AutoSchedule.cpp as the default
# export HL_MACHINE_PARAMS=32,24000000,160

export HL_PERMIT_FAILED_UNROLL=1
export HL_WEIGHTS_DIR=${HALIDE}/apps/autoscheduler/gpu_weights
export HL_TARGET=host-cuda

# no random dropout
export HL_RANDOM_DROPOUT=100

# greedy
 export HL_BEAM_SIZE=1
 export HL_NUM_PASSES=1

# beam search
# export HL_BEAM_SIZE=32
# export HL_NUM_PASSES=5

# ablation where we restrict to old space
# export HL_NO_SUBTILING=1

# ablation where instead of coarse to fine, we just enlarge the beam
#export HL_BEAM_SIZE=160
#export HL_NUM_PASSES=1

APPS="bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate_generator conv_layer mat_mul_generator iir_blur_generator resnet_50 bgu"

# Uncomment when there's a change that wouldn't be picked up by the Makefiles (e.g. new weights)
for app in ${APPS}; do make -C ${HALIDE}/apps/${app} clean; done

for app in ${APPS}; do
    echo "Compile" $app
    # The apps sadly do not use consistent names for their test binary, but they're all either 'filter' or 'process'
    if [ -f ${HALIDE}/apps/${app}/filter.cpp ]; then
        make -j32 -C ${HALIDE}/apps/${app} bin/filter 2>stderr_${app}.txt >stdout_${app}.txt &
    else
        make -j32 -C ${HALIDE}/apps/${app} bin/process 2>stderr_${app}.txt >stdout_${app}.txt &
    fi
done
make -C ${HALIDE}/apps/resnet_50 bin/pytorch_weights/ok > /dev/null 2> /dev/null
wait

# benchmark everything
for app in ${APPS}; do
    echo "Bench" $app
    make -C ${HALIDE}/apps/${app} test > results_${app}.txt;
done

# Report results
for app in ${APPS}; do
    if [ $app == "resnet_50" ]; then
        C=$(grep "schedule_type=classic_auto_schedule" results_resnet_50.txt | cut -d" " -f 4)
        A=$(grep "schedule_type=_auto_schedule" results_resnet_50.txt | cut -d" " -f 4)
    else
        C=$(grep "Classic" results_${app}.txt -m 1 | cut -d" " -f4)
        A=$(grep "Auto" results_${app}.txt -m 1 | cut -d" " -f3)
    fi
    echo $app $C $A
done

#!/bin/bash

function find_halide() {
    local -n halide_root_ref=$1
    local -r silent=$2 || 0
    local dir=$(pwd)

    for i in {1..5}; do
        if [[ -f ${dir}/distrib/include/Halide.h ]]; then
            halide_root_ref=$(cd ${dir}; pwd)
            if [[ $silent -ne 1 ]]; then
                echo "Using Halide in ${halide_root_ref}"
            fi
            return 0
        fi
        dir=${dir}/..
    done

    echo "Unable to find Halide. Try re-running $(basename $0) from somewhere in the Halide tree."
    exit
}

function make_dir_path_absolute() {
    local -r path=$1
    local -n absolute_path_ref=$2
    absolute_path_ref=$(cd ${path}; pwd)
}

function make_file_path_absolute() {
    local -r path=$1
    local -n converted_path_ref=$2
    converted_path_ref=$(cd $(dirname ${path}); pwd)/$(basename ${path})
}

function get_autoscheduler_dir() {
    local -r halide_root=$1
    local -n autoscheduler_dir_ref=$2
    autoscheduler_dir_ref=${halide_root}/apps/autoscheduler
}

function get_autoscheduler_bin_dir() {
    local -r halide_root=$1
    local -n autoscheduler_bin_dir_ref=$2
    get_autoscheduler_dir $halide_root autoscheduler_dir
    autoscheduler_bin_dir_ref=${autoscheduler_dir}/bin
}

function get_autoscheduler_scripts_dir() {
    local -r halide_root=$1
    local -n autoscheduler_scripts_dir_ref=$2
    autoscheduler_scripts_dir_ref=${halide_root}/apps/autoscheduler/scripts
}

function build_featurization_to_sample() {
    local -r halide_root=$1
    get_autoscheduler_dir $halide_root autoscheduler_dir
    get_autoscheduler_bin_dir $halide_root autoscheduler_bin_dir

    echo
    echo "Building featurization_to_sample..."
    make -C ${autoscheduler_dir} ${autoscheduler_bin_dir}/featurization_to_sample
    echo
}

function build_libauto_schedule() {
    local -r halide_root=$1
    get_autoscheduler_dir $halide_root autoscheduler_dir
    get_autoscheduler_bin_dir $halide_root autoscheduler_bin_dir

    echo
    echo "Building libauto_schedule..."
    make -C ${autoscheduler_dir} ${autoscheduler_bin_dir}/libauto_schedule.so
    echo
}

function build_train_cost_model() {
    local -r halide_root=$1
    get_autoscheduler_dir $halide_root autoscheduler_dir
    get_autoscheduler_bin_dir $halide_root autoscheduler_bin_dir

    echo
    echo "Building retrain_cost_model..."
    make -C ${autoscheduler_dir} ${autoscheduler_bin_dir}/retrain_cost_model
    echo
}

function build_autoscheduler_tools() {
    local -r halide_root=$1
    get_autoscheduler_dir $halide_root autoscheduler_dir

    echo
    echo "Building autoscheduler tools..."
    build_featurization_to_sample $halide_root
    build_train_cost_model $halide_root
    build_libauto_schedule $halide_root
    echo
}

function retrain_cost_model() {
    local -r halide_root=$1
    local -r samples_dir=$2
    local -r weights=$3
    local -r num_cores=$4
    local -r num_epochs=$5
    local -r pipeline_id=$6
    local -r predictions_file=${7-""}

    get_autoscheduler_bin_dir ${halide_root} autosched_bin

    find ${samples_dir} -name "*.sample" | \
        ${autosched_bin}/retrain_cost_model \
            --epochs=${num_epochs} \
            --rates="0.0001" \
            --num_cores=${num_cores} \
            --initial_weights=${weights} \
            --weights_out=${weights} \
            --best_benchmark=${samples_dir}/best.${pipeline_id}.benchmark.txt \
            --best_schedule=${samples_dir}/best.${pipeline_id}.schedule.h
}

function find_equal_predicted_pairs() {
    local -r limit=$1
    sort ${2} -k2 -n | awk -F', ' -f find_equal_predicted_pairs.awk | sort -k6 -n -r | head -n ${limit}
}

function find_similar_predicted_pairs() {
    local -r limit=$1
    sort ${2} -k2 -n | awk -F', ' -f find_similar_predicted_pairs.awk | sort -k9 -n -r | head -n ${limit}
}

function get_timeout_cmd() {
    local -n timeout_cmd_ref=$1

    timeout_cmd_ref="timeout"
    if [ $(uname -s) = "Darwin" ] && ! which $timeout_cmd_ref 2>&1 >/dev/null; then
        # OSX doesn't have timeout; gtimeout is equivalent and available via Homebrew
        timeout_cmd_ref="gtimeout"
        if ! which $timeout_cmd_ref 2>&1 >/dev/null; then
            echo "Can't find the command 'gtimeout'. Run 'brew install coreutils' to install it."
            exit 1
        fi
    fi
}

function profile_gpu_sample() {
    local -r sample_dir=$1
    local -r output_dir=$2

    if [ -n $3 ]; then
        local -r timeout_cmd=$3
    else
        get_timeout_cmd timeout_cmd
    fi

    local -r num_cores=80
    local -r timeout=60s

    if [ ! -f ${sample_dir}/bench ]; then
        echo "${sample_dir}/bench not found."
        return 1
    fi

    local -r batch_id=$(basename $(dirname ${sample_dir}))
    local -r sample_id=$(basename ${sample_dir})
    local -r prefix=${batch_id}_sample_${sample_id}

    nvprof_timeline_cmd="HL_NUM_THREADS=${num_cores} \
        ${timeout_cmd} -k ${timeout} ${timeout} \
        nvprof -f --output-profile ${output_dir}/${prefix}_timeline.nvprof \
        ${sample_dir}/bench \
        --output_extents=estimate \
        --default_input_buffers=random:0:estimate_then_auto \
        --default_input_scalars=estimate \
        --benchmarks=all \
        --benchmark_max_iters=1"

    local -r nvprof_metrics_cmd="HL_NUM_THREADS=${num_cores} \
        ${timeout_cmd} -k ${timeout} ${timeout} \
        nvprof -f --analysis-metrics -o ${output_dir}/${prefix}_metrics.nvprof \
        ${sample_dir}/bench \
        --output_extents=estimate \
        --default_input_buffers=random:0:estimate_then_auto \
        --default_input_scalars=estimate \
        --benchmarks=all \
        --benchmark_max_iters=1"

    eval "${nvprof_timeline_cmd} && ${nvprof_metrics_cmd}"
    return 0
}

function predict_all() {
    local -r halide_root=$1
    local -r samples_dir=$2
    local -r weights_dir=$3
    local -r predictions_file=$4

    get_autoscheduler_scripts_dir ${halide_root} scripts_dir
    bash ${scripts_dir}/predict_all.sh ${samples_dir} ${weights_dir} ${predictions_file}
}

function extract_best_times() {
    local -r halide_root=$1
    local -r samples_dir=$2
    local -r output_file=$3

    get_autoscheduler_scripts_dir ${halide_root} scripts_dir
    bash ${scripts_dir}/extract_best_times.sh ${samples_dir} ${output_file}
}

function average_compile_time_beam_search() {
    local -r samples_dir=$1

    grep "Compile time" ${samples_dir}/*/0/compile_err.txt | awk -F" " '{sum += $4}; END{printf("Average beam search compile time: %fs\n", sum / NR)}'
}

function average_compile_time_greedy() {
    local -r samples_dir=$1

    grep "Compile time" ${samples_dir}/*/*/compile_err.txt | awk -F" " '$1 !~ /\/0\/compile_err.txt:Compile$/ {sum += $4}; {count += 1}; END{printf("Average greedy compile time: %fs\n", sum / count)}'
}

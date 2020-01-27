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

function get_absolute_autoscheduler_bin_dir() {
    local -r halide_root=$1
    local -n autoscheduler_bin_dir_ref=$2
    get_autoscheduler_dir $halide_root autoscheduler_dir
    autoscheduler_bin_dir_ref=${autoscheduler_dir}/bin
}

function get_autoscheduler_bin_dir() {
    local -n autoscheduler_bin_dir_ref=$1
    autoscheduler_bin_dir_ref=bin
}

function get_autoscheduler_make_bin_dir() {
    local -n autoscheduler_bin_dir_ref=$1
    autoscheduler_bin_dir_ref=../autoscheduler/bin
}

function get_autoscheduler_scripts_dir() {
    local -r halide_root=$1
    local -n autoscheduler_scripts_dir_ref=$2
    autoscheduler_scripts_dir_ref=${halide_root}/apps/autoscheduler/scripts
}

function build_featurization_to_sample() {
    local -r halide_root=$1
    get_autoscheduler_dir $halide_root autoscheduler_dir
    get_absolute_autoscheduler_bin_dir $halide_root autoscheduler_bin_dir

    echo
    echo "Building featurization_to_sample..."
    make -C ${autoscheduler_dir} ${autoscheduler_bin_dir}/featurization_to_sample
    echo
}

function build_libauto_schedule() {
    local -r halide_root=$1
    get_autoscheduler_dir $halide_root autoscheduler_dir
    get_absolute_autoscheduler_bin_dir $halide_root autoscheduler_bin_dir

    echo
    echo "Building libauto_schedule..."
    make -C ${autoscheduler_dir} ${autoscheduler_bin_dir}/libauto_schedule.so
    echo
}

function build_retrain_cost_model() {
    local -r halide_root=$1
    get_autoscheduler_dir $halide_root autoscheduler_dir
    get_absolute_autoscheduler_bin_dir $halide_root autoscheduler_bin_dir

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
    build_retrain_cost_model $halide_root
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
    local -r verbose=${8-0}
    local -r partition_schedules=${9-0}

    get_absolute_autoscheduler_bin_dir ${halide_root} autosched_bin

    find ${samples_dir} -name "*.sample" | \
        ${autosched_bin}/retrain_cost_model \
            --epochs=${num_epochs} \
            --rates="0.0001" \
            --num_cores=${num_cores} \
            --initial_weights=${weights} \
            --weights_out=${weights} \
            --best_benchmark=${samples_dir}/best.${pipeline_id}.benchmark.txt \
            --best_schedule=${samples_dir}/best.${pipeline_id}.schedule.h \
            --predictions_file=${predictions_file} \
            --verbose=${verbose} \
            --partition_schedules=${partition_schedules}
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

function reset_weights() {
    local -r halide_root=$1
    local -r weights=$2

    get_absolute_autoscheduler_bin_dir ${halide_root} autosched_bin

    ${autosched_bin}/retrain_cost_model \
        --initial_weights=${weights} \
        --weights_out=${weights} \
        --randomize_weights=1 \
        --reset_weights=1 \
        --epochs=1 \
        --rates="0.0001" \
        --num_cores=1 \
        --best_benchmark="" \
        --best_schedule="" \
        --predictions_file="" \
        --partition_schedules=0 \
        --verbose=0
}

function extract_sample_details() {
    local -r sample_dir=$1
    local -r output_dir=$2

    local -r output_file=$output_dir/best.txt

    local -r compile_err=${sample_dir}/compile_err.txt
    local -r bench=${sample_dir}/bench.txt
    local -r weights=$(dirname ${sample_dir})/used.weights

    local -r start_line=$(grep -n "Optimal schedule" ${compile_err} | cut -d":" -f 1)
    local -r end_line=$(grep -n "Number of states added" ${compile_err} | cut -d":" -f 1)

    touch ${output_file}

    head -n $((end_line - 1)) ${compile_err} | tail -n $((end_line - start_line)) > "${output_file}"

    echo "" >> ${output_file}
    local -r git_hash=$(grep "git rev-parse" ${compile_err} | tail -n 1 | cut -d" " -f 6)
    echo "git rev-parse --verify HEAD = ${git_hash}" >> ${output_file}

    local -r time=$(head -n 1 ${bench} | cut -d" " -f 8)

    local -r time_ms=$(echo "${time} * 1000" | bc -l | awk '{printf "%.6f\n", $0}')
    echo "" >> ${output_file}
    echo "Run time (ms) = ${time_ms}" >> ${output_file}

    cp ${weights} ${output_dir}/best.weights
}

function extract_best_sample_details() {
    local -r samples_dir=$1

    local -r sample_file=$(grep "Best runtime" ${samples_dir}/autotune_out.txt | tail -n 1 | cut -d" " -f 12)

    local -r best_dir=$(dirname $sample_file)

    extract_sample_details ${best_dir} ${samples_dir}
}

function save_best_schedule_result() {
    local -r results_dir=$1
    local -r samples_dir=$2

    local -r app=$(basename $(dirname $samples_dir))

    echo "Comparing candidate results with current best for ${app}"

    local -r candidate_details_file=${samples_dir}/best.txt
    local -r generator_name=${app#"cuda_"}
    local -r candidate_schedule_file=${samples_dir}/best.${generator_name%"_generator"}.schedule.h
    local -r candidate_weights_file=${samples_dir}/best.weights

    if [ ! -f $candidate_schedule_file ]; then
        echo "${candidate_schedule_file} not found. Exiting..."
        return
    fi

    extract_best_sample_details ${samples_dir}

    local -r best_details_file=${results_dir}/$app.txt
    local -r best_schedule_file=${results_dir}/${app}.h
    local -r best_weights_file=${results_dir}/${app}.weights

    local -r candidate_run_time=$(tail -n 1 $candidate_details_file | cut -d" " -f 5)

    if [ ! -f $best_details_file ]; then
        echo "$best_details_file not found. Copying in candidate (${candidate_run_time} ms) files as new best results..."
        cp $candidate_details_file $best_details_file
        cp $candidate_schedule_file $best_schedule_file
        cp $candidate_weights_file $best_weights_file
        return
    fi

    local -r current_best_run_time=$(tail -n 1 $best_details_file | cut -d" " -f 5)

    local -r new_best=$(echo "$candidate_run_time < $current_best_run_time" | bc -l)
    if [ $new_best -eq 1 ]; then
        echo "Candidate run time (${candidate_run_time} ms) is faster than the current best run time (${current_best_run_time} ms). Copying in candidate files as new best results..."
        cp $candidate_details_file $best_details_file
        cp $candidate_schedule_file $best_schedule_file
        cp $candidate_weights_file $best_weights_file
    else
        echo "Candidate run time (${candidate_run_time} ms) is not faster than the current best run time (${current_best_run_time} ms)"
    fi
}

function print_best_schedule_times() {
    local -r dir=$1

    local -r apps="resnet_50_blockwise bgu bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate_generator conv_layer cuda_mat_mul iir_blur_generator"

    for app in $apps; do
        local file=$dir/$app.txt
        if [ ! -f $file ]; then
            echo "$app not found."
            continue
        fi

        local current_best_run_time=$(tail -n 1 $file | cut -d" " -f 5)
        echo "$app: $current_best_run_time ms"
    done
}

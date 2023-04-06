#!/bin/bash

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

function get_halide_src_dir() {
    local -r autoscheduler_src_dir=$1
    local -n halide_src_dir_ref=$2
    make_dir_path_absolute ${autoscheduler_src_dir}/../../../ halide_src_dir_ref
}

function get_autoscheduler_src_dir() {
    local -r halide_src_dir=$1
    local -n autoscheduler_dir_ref=$2
    autoscheduler_dir_ref=${halide_src_dir}/src/autoschedulers/anderson2021
}

function get_autoscheduler_build_dir() {
    local -r halide_build_dir=$1
    local -n autoscheduler_build_dir_ref=$2
    autoscheduler_build_dir_ref=${halide_build_dir}/src/autoschedulers/anderson2021
}

function get_tools_build_dir() {
    local -r halide_build_dir=$1
    local -n tools_build_dir_ref=$2
    tools_build_dir_ref=${halide_build_dir}/tools
}

function get_autoscheduler_scripts_dir() {
    local -r halide_src_dir=$1
    local -n autoscheduler_scripts_dir_ref=$2
    get_autoscheduler_src_dir $halide_src_dir autoscheduler_src_dir
    autoscheduler_scripts_dir_ref=${autoscheduler_src_dir}/scripts
}

function get_host_target() {
    local -r autoscheduler_build_dir=$1
    local -n host_target_ref=$2

    echo "Calling get_host_target()..."
    host_target_ref=$(${autoscheduler_build_dir}/get_host_target)
    echo "host_target = ${host_target_ref}"
    echo
}

function retrain_cost_model() {
    local -r halide_build_dir=$1
    local -r samples_dir=$2
    local -r weights=$3
    local -r num_cores=$4
    local -r num_epochs=$5
    local -r pipeline_id=$6
    local -r learning_rate=${7-0.001}
    local -r predictions_file=${8-""}
    local -r verbose=${9-0}
    local -r partition_schedules=${10-0}
    local -r limit=${11-0}

    get_autoscheduler_build_dir ${halide_build_dir} autoscheduler_build_dir

    echo "Using learning rate: ${learning_rate}"

    find ${samples_dir} -name "*.sample" | \
         HL_NUM_THREADS=8 ${autoscheduler_build_dir}/anderson2021_retrain_cost_model \
            --epochs=${num_epochs} \
            --rates=${learning_rate} \
            --num_cores=${num_cores} \
            --initial_weights=${weights} \
            --weights_out=${weights} \
            --best_benchmark=${samples_dir}/best.${pipeline_id}.benchmark.txt \
            --best_schedule=${samples_dir}/best.${pipeline_id}.schedule.h \
            --predictions_file=${predictions_file} \
            --verbose=${verbose} \
            --partition_schedules=${partition_schedules} \
            --limit=${limit}
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

function predict_all() {
    local -r halide_src_dir=$1
    local -r halide_build_dir=$2
    local -r samples_dir=$3
    local -r weights_dir=$4
    local -r predictions_file=$5
    local -r include_filenames=$6
    local -r limit=$7
    local -r parallelism=$8

    get_autoscheduler_scripts_dir ${halide_src_dir} scripts_dir
    bash ${scripts_dir}/predict_all.sh ${halide_build_dir} ${samples_dir} ${weights_dir} ${predictions_file} ${include_filenames} ${limit} ${parallelism}
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
    local -r halide_build_dir=$1
    local -r weights=$2

    get_autoscheduler_build_dir ${halide_build_dir} autoscheduler_build_dir

    ${autoscheduler_build_dir}/anderson2021_retrain_cost_model \
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

    local -r time_ms=$(echo "${time}" | awk '{printf "%.6f\n", $0 * 1000}')
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
    mkdir -p ${results_dir}

    local -r app=$(basename $(dirname $samples_dir))

    echo "Comparing candidate results with current best for ${app}..."

    local -r candidate_details_file=${samples_dir}/best.txt
    local -r generator_name=${app#"cuda_"}
    local -r candidate_schedule_file=${samples_dir}/best.${generator_name}.schedule.h
    local -r candidate_weights_file=${samples_dir}/best.weights
    local -r candidate_autotune_out_file=${samples_dir}/autotune_out.txt

    if [ ! -f $candidate_schedule_file ]; then
        echo "${candidate_schedule_file} not found. Exiting..."
        return
    fi

    extract_best_sample_details ${samples_dir}

    local -r best_details_file=${results_dir}/$app.txt
    local -r best_schedule_file=${results_dir}/${app}.h
    local -r best_weights_file=${results_dir}/${app}.weights
    local -r best_autotune_out_file=${results_dir}/${app}.autotune_out

    local -r candidate_run_time=$(tail -n 1 $candidate_details_file | cut -d" " -f 5)

    if [ ! -f $best_details_file ]; then
        echo "$best_details_file not found. Copying in candidate (${candidate_run_time} ms) files as new best results..."
        cp $candidate_details_file $best_details_file
        cp $candidate_schedule_file $best_schedule_file
        cp $candidate_weights_file $best_weights_file
        cp $candidate_autotune_out_file $best_autotune_out_file
        return
    fi

    local current_best_run_time=$(tail -n 1 $best_details_file | cut -d" " -f 5)

    local new_best=1
    if [ ${current_best_run_time} ]; then
        new_best=$(echo "$candidate_run_time < $current_best_run_time" | bc -l)
    else
        current_best_run_time="?"
    fi

    if [ $new_best -eq 1 ]; then
        echo "Candidate run time (${candidate_run_time} ms) is faster than the current best run time (${current_best_run_time} ms). Copying in candidate files as new best results..."
        cp $candidate_details_file $best_details_file
        cp $candidate_schedule_file $best_schedule_file
        cp $candidate_weights_file $best_weights_file
        cp $candidate_autotune_out_file $best_autotune_out_file
    else
        echo "Candidate run time (${candidate_run_time} ms) is not faster than the current best run time (${current_best_run_time} ms)"
    fi
}

function print_best_schedule_times() {
    local -r dir=$1

    local -r apps="bgu bilateral_grid local_laplacian nl_means lens_blur camera_pipe stencil_chain harris hist max_filter unsharp interpolate conv_layer cuda_mat_mul iir_blur depthwise_separable_conv"

    echo "Best found schedule times:"
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

function get_num_cpu_cores() {
    local -n num_cpu_cores_ref=$1

    if [ $(uname -s) = "Darwin" ]; then
        num_cpu_cores_ref=$(sysctl -n hw.ncpu)
    else
        num_cpu_cores_ref=$(nproc)
    fi
}

function find_unused_gpu() {
    local -r benchmark_queue_dir=$1
    local -r num_gpus=$2
    local -n gpu_id_ref=$3

    for ((index=0;index<num_gpus;index++)); do
        exists=0
        # If a GPU is in use in the benchmark queue, a file will have the suffix
        # _gpu_${index}
        for f in ${benchmark_queue_dir}/*-gpu_${index}; do
            [ -e "$f" ] && exists=1
            break
        done

        if [[ $exists == 0 ]]; then
            gpu_id_ref=${index}
            return 0
        fi
    done

    return 1
}

function get_bench_args() {
    local -r images_dir=$1
    local -r app=$2
    local -r sample_dir=$3
    local -n bench_args_ref=$4

    case $app in
        "bgu") bench_args_ref="splat_loc=${images_dir}/low_res_in.png values=${images_dir}/low_res_out.png slice_loc=${images_dir}/rgb.png r_sigma=0.125 s_sigma=16" ;;
        "bilateral_grid") bench_args_ref="input=${images_dir}/gray.png r_sigma=0.1" ;;
        "local_laplacian") bench_args_ref="input=${images_dir}/rgb.png levels=8 alpha=1 beta=1" ;;
        "lens_blur") bench_args_ref="left_im=${images_dir}/rgb.png right_im=${images_dir}/rgb.png slices=32 focus_depth=13 blur_radius_scale=0.5 aperture_samples=32" ;;
        "nl_means") bench_args_ref="input=${images_dir}/rgb.png patch_size=7 search_area=7 sigma=0.12" ;;
        "camera_pipe") bench_args_ref="input=${images_dir}/bayer_raw.png matrix_3200=${images_dir}/matrix_3200.mat matrix_7000=${images_dir}/matrix_7000.mat color_temp=3700 gamma=2.0 contrast=50 sharpen_strength=1.0 blackLevel=25 whiteLevel=1023" ;;
        "stencil_chain") bench_args_ref="input=${images_dir}/rgb.png" ;;
        "harris") bench_args_ref="input=${images_dir}/rgba.png" ;;
        "hist") bench_args_ref="input=${images_dir}/rgb.png" ;;
        "max_filter") bench_args_ref="input=${images_dir}/rgb.png" ;;
        "unsharp") bench_args_ref="input=${images_dir}/rgba.png" ;;
        "interpolate") bench_args_ref="input=${images_dir}/rgba.png" ;;
        "conv_layer") bench_args_ref="--estimate_all" ;;
        "cuda_mat_mul") bench_args_ref="A=zero:estimate B=zero:estimate" ;;
        "iir_blur") bench_args_ref="input=${images_dir}/rgba.png alpha=0.5" ;;
        "depthwise_separable_conv") bench_args_ref="--estimate_all" ;;
        *) bench_args_ref="--estimate_all" ;;
    esac
}

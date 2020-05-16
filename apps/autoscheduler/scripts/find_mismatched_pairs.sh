#!/bin/bash

# Find pairs of schedules with equal/similar predicted run times but different
# actual run times

if [[ ! ((($# == 5 || $# == 6) && ${5} == 0) || ($# == 6 && ${5} == 1)) ]]; then
  echo "Usage: $0 app timestamp limit equal_mode profile_mode output_dir"
  exit
fi

source $(dirname $0)/utils.sh

find_halide HALIDE_ROOT

app=${1}
timestamp=${2}
limit=${3}
exact_mode=${4}
profile_mode=${5}
output_dir=${6}
predictions_file="${HALIDE_ROOT}/apps/${app}/autotuned_samples-${timestamp}/predictions_with_filenames"

if [ ${exact_mode} == 1 ]; then
    echo "Pairs with equal predicted run times (predicted, actual_1, actual_2, abs_diff_of_actual_1_and_2):"
    func=find_equal_predicted_pairs
else
    echo "Pairs with similar predicted run times (predicted_1, predicted_2, actual_1, actual_2, normalized_abs_diff_of_predicted_1_and_2, normalized_abs_diff_of_actual_1_and_2, normalized_abs_diff_of_actual_1_and_2 / normalized_abs_diff_of_predicted_1_and_2):"
    func=find_similar_predicted_pairs
fi

cat ${predictions_file} | ${func} ${limit}

if [[ $profile_mode != 1 ]]; then
  exit
fi

echo
echo "Profiling and saving resuls to ${output_dir}..."

sample_dirs=$(cat ${predictions_file} | ${func} ${limit} | awk -F" " '{printf("%s\n%s\n", $1, $2);}' | xargs dirname)

mkdir -p ${output_dir}

for sample_dir in $sample_dirs; do
  profile_gpu_sample ${sample_dir} ${output_dir}
done



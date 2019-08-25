#!/bin/bash

# Find pairs of schedules with similar predicted run times but different
# actual run times

if [ $# -ne 3 ]; then
  echo "Usage: $0 predictions_file limit profile_mode"
  exit
fi

source $(dirname $0)/utils.sh

predictions_file=${1}
limit=${2}
profile_mode=${3}

echo "Pairs with similar predicted run times (predicted_1, predicted_2, actual_1, actual_2, normalized_abs_diff_of_predicted_1_and_2, normalized_abs_diff_of_actual_1_and_2, normalized_abs_diff_of_actual_1_and_2 / normalized_abs_diff_of_predicted_1_and_2):"
cat ${predictions_file} | find_similar_predicted_pairs ${limit}

if [[ $profile_mode != 1 ]]; then
  exit
fi

echo
echo "Profiling..."

sample_dirs=$(cat ${predictions_file} | find_similar_predicted_pairs ${limit} | awk -F" " '{printf("%s\n%s\n", $1, $2);}' | xargs dirname)

for sample_dir in $sample_dirs; do
  profile_gpu_sample ${sample_dir}
done

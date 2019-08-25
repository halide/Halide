#!/bin/bash

# Find pairs of schedules with similar predicted run times but different
# actual run times

if [ $# -ne 2 ]; then
  echo "Usage: $0 predictions_file limit"
  exit
fi

source $(dirname $0)/utils.sh

predictions_file=${1}
limit=${2}

echo "Pairs with similar predicted run times (predicted_1, predicted_2, actual_1, actual_2, normalized_abs_diff_of_predicted_1_and_2, normalized_abs_diff_of_actual_1_and_2, normalized_abs_diff_of_actual_1_and_2 / normalized_abs_diff_of_predicted_1_and_2):"
cat ${predictions_file} | find_similar_predicted_pairs ${limit}

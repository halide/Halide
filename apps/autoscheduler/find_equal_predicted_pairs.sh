#!/bin/bash

# Find pairs of schedules with equal predicted run times but different
# actual run times

if [ $# -ne 2 ]; then
  echo "Usage: $0 predictions_file limit"
  exit
fi

predictions_file=${1}
limit=${2}

echo "Pairs with equal predicted run times (predicted, actual_1, actual_2, abs_diff_of_actual_1_and_2):"
sort ${predictions_file} -k2 -n | awk -F', ' -f find_equal_predicted_pairs.awk | sort -k6 -n -r | head -n ${limit}

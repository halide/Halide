import argparse
from pathlib import Path
import pdb
import sh
import os

def parse_formatted(filename):
  result = {}
  with open(filename) as file:
    for line in file:
      stage, key, value = line.split()
      if not stage in result:
        result[stage] = {}

      result[stage][key] = float(value)

  return result

def global_load_efficiency(metrics, features):
  actual = metrics["gld_efficiency"]
  predicted = features["global_mem_load_efficiency"] * 100
  return actual, predicted

def global_store_efficiency(metrics, features):
  actual = metrics["gst_efficiency"]
  predicted = features["global_mem_store_efficiency"] * 100
  return actual, predicted

def global_load_transactions(metrics, features):
  actual = metrics["gld_transactions"]
  predicted = features["num_global_mem_loads_per_block"] * features["num_blocks"]
  return actual, predicted

def global_store_transactions(metrics, features):
  actual = metrics["gst_transactions"]
  predicted = features["num_global_mem_stores_per_block"] * features["num_blocks"]
  return actual, predicted

def compare_metrics_and_features(metrics_filename, features_filename):
  metrics = parse_formatted(metrics_filename)
  features = parse_formatted(features_filename)

  comparisons = {}
  comparisons["global load transactions"] = global_load_transactions
  comparisons["global store transactions"] = global_store_transactions
  comparisons["global load efficiency"] = global_load_efficiency
  comparisons["global store efficiency"] = global_store_efficiency

  for stage in features:
    print("{}:".format(stage))
    for label in comparisons:
      actual, predicted = comparisons[label](metrics[stage], features[stage])
      ratio = 100 * abs(predicted - actual) / actual
      print("  {}: {} {} {:.2f}".format(label, actual, predicted, ratio))

  return

  outliers = [Path(o.strip().split(",")[0]) for o in outliers]
  dirs = [o.parent for o in outliers]

  compile_err_files = [Path("{}/compile_err.txt".format(d)) for d in dirs]

  for outlier in outliers:
    compile_err_file = Path("{}/compile_err.txt".format(outlier.parent))

    sample_id = outlier.parent.name
    batch_id = outlier.parent.parent.name

    metrics_filename = Path("{}/basic_metrics_{}_{}.log".format(outlier.parent, batch_id, sample_id))
    print(sample_id)
    print(batch_id)
    print(metrics_filename)

    if not metrics_filename.is_file():
      print("Collecting metrics for {}...".format(metrics_filename))
      #bash "${dir}/metrics_command.txt"

    stages = sh.grep("features", compile_err_file).strip().split()[3]

    for stage in [stages]:
      metrics = {}
      metrics_list = sh.grep("-A 11", "kernel_".format(stage), metrics_filename).strip().splitlines()
      for line in metrics_list[1:]:
        line = line.split()
        value = line[-3]
        if value[-1] == "%":
          value = value[0:-1]
        metrics[line[1]] = float(value)
      print(metrics)



if __name__ == "__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument("--formatted_metrics", type=str, required=True)
  parser.add_argument("--formatted_features", type=str, required=True)
  #parser.add_argument("--N", type=int, required=True)

  args = parser.parse_args()

  compare_metrics_and_features(Path(args.formatted_metrics), Path(args.formatted_features))

#function extract_metric() {
    #local -r metrics_file=$1
    #local -r stage=$2
    #local -r metric_name=$3

    #grep -A 11 "kernel_${s}" "${metrics_file}" | grep "${metric_name}" | head -n 1 | awk '{printf("%f\n", $6);}'
#}

#function compare_with_profiler() {
    #local -r dir=$1

    #local -r stages=$(grep "features" ${dir}/compile_err.txt | cut -d" " -f 4)

    #local -r sample_id=$(basename $dir)
    #local -r batch_id=$(basename $(dirname $dir))
    #local -r metrics_file="${dir}/basic_metrics_${batch_id}_${sample_id}.log"

    #if [ ! -f ${metrics_file} ]; then
        #echo "Collecting metrics for ${dir}..."
        #bash "${dir}/metrics_command.txt"
    #fi

    #for s in ${stages}; do
        #gld_transactions=$(extract_metric ${metrics_file} ${s} gld_transactions)

        #features=$(grep -A 87 "features for ${s}" ${dir}/compile_err.txt)
        #lds=$(echo "${features}" | grep num_global_mem_loads | awk '{printf("%f\n", $2)}')
        #num_blocks=$(echo "${features}" | grep num_blocks | awk '{printf("%f\n", $2)}')

        #feature_transactions=$(echo "${lds} * ${num_blocks}" | bc)
        #ratio=$(echo "sqrt((${feature_transactions} - ${feature_transactions}) * (${feature_transactions} - ${feature_transactions})) / ${gld_transactions}" | bc)

        #printf "${s}: %f %f %f\n" ${gld_transactions} ${feature_transactions} ${ratio}
    #done
#}

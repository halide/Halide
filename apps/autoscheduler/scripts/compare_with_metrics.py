import argparse
from pathlib import Path
import pdb
import sh
import os
import math

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

  if actual == 0:
    actual = 100

  return actual, predicted

def global_store_efficiency(metrics, features):
  actual = metrics["gst_efficiency"]
  predicted = features["global_mem_store_efficiency"] * 100

  if actual == 0:
    actual = 100

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
      if actual != 0:
        ratio = 100 * abs(predicted - actual) / actual
      else:
        if predicted == 0:
          ratio = 0
        else:
          ratio = math.inf
      print("  {}: {:.2f} {:.2f} {:.2f}".format(label, actual, predicted, ratio))


if __name__ == "__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument("--formatted_metrics", type=str, required=True)
  parser.add_argument("--formatted_features", type=str, required=True)

  args = parser.parse_args()

  compare_metrics_and_features(Path(args.formatted_metrics), Path(args.formatted_features))

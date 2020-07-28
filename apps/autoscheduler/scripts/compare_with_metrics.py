import argparse
from pathlib import Path
import pdb
import sh
import os
import math
from enum import Enum
import re

class DataResult:
  def __init__(self, value):
    self.value = value

  def __str__(self):
    return "{:>14.2f}".format(self.value)

class IntDataResult(DataResult):
  def __init__(self, value):
    super().__init__(value)

  def __str__(self):
    return "{:>14d}".format(int(self.value))

class Result:
  def __init__(self, actual, predicted):
    self.actual = actual
    self.predicted = predicted
    if actual != 0:
      self.ratio = 100 * abs(predicted - actual) / actual
    else:
      if predicted == 0:
        self.ratio = 0
      else:
        self.ratio = math.inf

    if min(predicted, actual) != 0:
      self.factor = max(predicted, actual) / min(predicted, actual)
      if predicted > actual:
        self.factor = -self.factor
    else:
      if max(predicted, actual) != 0:
        self.factor = math.inf
      else:
        self.factor = 0

  def __str__(self):
    return "{:>14.2f} {:>14.2f} {:>7.2f} {:>7.2f}".format(self.actual, self.predicted, self.ratio, self.factor)

class IntResult(Result):
  def __init__(self, actual, predicted):
    super().__init__(actual, predicted)

  def __str__(self):
    return "{:>14d} {:>14d} {:>7.2f} {:>7.2f}".format(int(self.actual), int(self.predicted), self.ratio, self.factor)

class Features(Enum):
  GLOBAL_LOAD_REQUESTS = "global load requests"
  GLOBAL_LOAD_TRANSACTIONS_PER_REQUEST = "global load transactions per request"
  GLOBAL_LOAD_TRANSACTIONS = "global load transactions"
  GLOBAL_STORE_REQUESTS = "global store requests"
  GLOBAL_STORE_TRANSACTIONS_PER_REQUEST = "global store transactions per request"
  GLOBAL_STORE_TRANSACTIONS = "global store transactions"
  SHARED_LOAD_REQUESTS = "shared load requests"
  SHARED_LOAD_TRANSACTIONS_PER_REQUEST = "shared load transactions per request"
  SHARED_LOAD_TRANSACTIONS = "shared load transactions"
  SHARED_STORE_REQUESTS = "shared store requests"
  SHARED_STORE_TRANSACTIONS_PER_REQUEST = "shared store transactions per request"
  SHARED_STORE_TRANSACTIONS = "shared store transactions"
  LOCAL_LOAD_REQUESTS = "local load requests"
  LOCAL_LOAD_TRANSACTIONS_PER_REQUEST = "local load transactions per request"
  LOCAL_LOAD_TRANSACTIONS = "local load transactions"
  LOCAL_STORE_REQUESTS = "local store requests"
  LOCAL_STORE_TRANSACTIONS_PER_REQUEST = "local store transactions per request"
  LOCAL_STORE_TRANSACTIONS = "local store transactions"
  GLOBAL_LOAD_EFFICIENCY = "global load efficiency"
  GLOBAL_STORE_EFFICIENCY = "global store efficiency"

class Data(Enum):
  REGISTERS_64 = "registers_64"
  REGISTERS_256 = "registers_256"

class Sample:
  def __init__(self, path, metrics_path, features_path):
    self.path = path
    self.metrics = self.parse_formatted(metrics_path)
    self.features = self.parse_formatted(features_path)
    self.results = {}
    self.data = {}

    self.comparisons = {}
    self.comparisons[Features.GLOBAL_LOAD_TRANSACTIONS] = self.global_load_transactions
    self.comparisons[Features.GLOBAL_LOAD_REQUESTS] = self.global_load_requests
    self.comparisons[Features.GLOBAL_LOAD_TRANSACTIONS_PER_REQUEST] = self.global_load_transactions_per_request
    self.comparisons[Features.GLOBAL_STORE_TRANSACTIONS] = self.global_store_transactions
    self.comparisons[Features.GLOBAL_STORE_REQUESTS] = self.global_store_requests
    self.comparisons[Features.GLOBAL_STORE_TRANSACTIONS_PER_REQUEST] = self.global_store_transactions_per_request
    self.comparisons[Features.GLOBAL_LOAD_EFFICIENCY] = self.global_load_efficiency
    self.comparisons[Features.GLOBAL_STORE_EFFICIENCY] = self.global_store_efficiency

    self.comparisons[Features.SHARED_LOAD_TRANSACTIONS] = self.shared_load_transactions
    self.comparisons[Features.SHARED_LOAD_REQUESTS] = self.shared_load_requests
    self.comparisons[Features.SHARED_LOAD_TRANSACTIONS_PER_REQUEST] = self.shared_load_transactions_per_request
    self.comparisons[Features.SHARED_STORE_TRANSACTIONS] = self.shared_store_transactions
    self.comparisons[Features.SHARED_STORE_REQUESTS] = self.shared_store_requests
    self.comparisons[Features.SHARED_STORE_TRANSACTIONS_PER_REQUEST] = self.shared_store_transactions_per_request

    self.comparisons[Features.LOCAL_LOAD_TRANSACTIONS] = self.local_load_transactions
    self.comparisons[Features.LOCAL_LOAD_REQUESTS] = self.local_load_requests
    self.comparisons[Features.LOCAL_LOAD_TRANSACTIONS_PER_REQUEST] = self.local_load_transactions_per_request
    self.comparisons[Features.LOCAL_STORE_TRANSACTIONS] = self.local_store_transactions
    self.comparisons[Features.LOCAL_STORE_REQUESTS] = self.local_store_requests
    self.comparisons[Features.LOCAL_STORE_TRANSACTIONS_PER_REQUEST] = self.local_store_transactions_per_request

    self.extract_data = {}

    self.ignore_list = [
      "^repeat_edge",
      "^lambda",
    ]

    self.success = self.compare_metrics_and_features()

  def should_ignore(self, stage):
    for ignore in self.ignore_list:
      if re.match(ignore, stage):
        return True
    return False

  def parse_formatted(self, filename):
    result = {}
    with open(filename) as file:
      for line in file:
        try:
          stage, key, value = line.split()
        except:
          continue
        if not stage in result:
          result[stage] = {}

        result[stage][key] = float(value)

    return result

  def global_load_efficiency(self, metrics, features):
    actual = metrics["gld_efficiency"]
    predicted = features["global_mem_load_efficiency"] * 100

    if actual == 0:
      actual = 100

    return Result(actual, predicted)

  def global_store_efficiency(self, metrics, features):
    actual = metrics["gst_efficiency"]
    predicted = features["global_mem_store_efficiency"] * 100

    if actual == 0:
      actual = 100

    return Result(actual, predicted)

  def global_load_transactions(self, metrics, features):
    actual = metrics["gld_transactions"]
    predicted = features["num_global_mem_load_transactions"]
    return IntResult(actual, predicted)

  def global_store_transactions(self, metrics, features):
    actual = metrics["gst_transactions"]
    predicted = features["num_global_mem_store_transactions"]
    return IntResult(actual, predicted)

  def global_load_requests(self, metrics, features):
    if metrics["gld_transactions_per_request"] == 0:
      return IntResult(0, 0)

    actual = metrics["gld_transactions"] / metrics["gld_transactions_per_request"]
    predicted = features["num_global_load_requests"]

    return IntResult(actual, predicted)

  def global_load_transactions_per_request(self, metrics, features):
    actual = metrics["gld_transactions_per_request"]
    try:
      predicted = features["num_global_load_transactions_per_request"]
    except:
      return Result(0, 0)

    return Result(actual, predicted)

  def global_store_requests(self, metrics, features):
    if metrics["gst_transactions_per_request"] == 0:
      return IntResult(0, 0)

    actual = metrics["gst_transactions"] / metrics["gst_transactions_per_request"]
    predicted = features["num_global_store_requests"]

    return IntResult(actual, predicted)

  def global_store_transactions_per_request(self, metrics, features):
    actual = metrics["gst_transactions_per_request"]
    try:
      predicted = features["num_global_store_transactions_per_request"]
    except:
      return Result(0, 0)

    return Result(actual, predicted)

  def get(self, metrics, key, default):
    if key in metrics:
      return metrics[key]

    return default

  def shared_load_transactions(self, metrics, features):
    actual = self.get(metrics, "shared_load_transactions", 0)
    predicted = features["num_shared_mem_load_transactions"]
    return IntResult(actual, predicted)

  def shared_store_transactions(self, metrics, features):
    actual = self.get(metrics, "shared_store_transactions", 0)
    predicted = features["num_shared_mem_store_transactions"]
    return IntResult(actual, predicted)

  def shared_load_requests(self, metrics, features):
    if self.get(metrics, "shared_load_transactions_per_request", 0) == 0:
      return IntResult(0, 0)

    actual = metrics["shared_load_transactions"] / metrics["shared_load_transactions_per_request"]
    predicted = features["num_shared_load_requests"]

    return IntResult(actual, predicted)

  def shared_load_transactions_per_request(self, metrics, features):
    actual = self.get(metrics, "shared_load_transactions_per_request", 0)
    try:
      predicted = features["num_shared_load_transactions_per_request"]
    except:
      return Result(0, 0)

    return Result(actual, predicted)

  def shared_store_requests(self, metrics, features):
    if self.get(metrics, "shared_store_transactions_per_request", 0) == 0:
      return IntResult(0, 0)

    actual = metrics["shared_store_transactions"] / metrics["shared_store_transactions_per_request"]
    predicted = features["num_shared_store_requests"]

    return IntResult(actual, predicted)

  def shared_store_transactions_per_request(self, metrics, features):
    actual = self.get(metrics, "shared_store_transactions_per_request", 0)
    try:
      predicted = features["num_shared_store_transactions_per_request"]
    except:
      return Result(0, 0)

    return Result(actual, predicted)

  def local_load_transactions(self, metrics, features):
    actual = self.get(metrics, "local_load_transactions", 0)
    predicted = self.get(features, "num_local_mem_load_transactions", 0)
    return IntResult(actual, predicted)

  def local_store_transactions(self, metrics, features):
    actual = self.get(metrics, "local_store_transactions", 0)
    predicted = self.get(features, "num_local_mem_store_transactions", 0)
    return IntResult(actual, predicted)

  def local_load_requests(self, metrics, features):
    if self.get(metrics, "local_load_transactions_per_request", 0) == 0:
      return IntResult(0, 0)

    actual = metrics["local_load_transactions"] / metrics["local_load_transactions_per_request"]
    predicted = self.get(features, "num_local_load_requests", 0)

    return IntResult(actual, predicted)

  def local_load_transactions_per_request(self, metrics, features):
    actual = self.get(metrics, "local_load_transactions_per_request", 0)
    predicted = self.get(features, "num_local_load_transactions_per_request", 0)

    return Result(actual, predicted)

  def local_store_requests(self, metrics, features):
    if self.get(metrics, "local_store_transactions_per_request", 0) == 0:
      return IntResult(0, 0)

    actual = metrics["local_store_transactions"] / metrics["local_store_transactions_per_request"]
    predicted = self.get(features, "num_local_store_requests", 0)

    return IntResult(actual, predicted)

  def local_store_transactions_per_request(self, metrics, features):
    actual = self.get(metrics, "local_store_transactions_per_request", 0)
    predicted = self.get(features, "num_local_store_transactions_per_request", 0)

    return Result(actual, predicted)

  def compare_metrics_and_features(self):
    for stage in self.features:
      self.results[stage] = {}
      self.data[stage] = {}

      if not stage in self.metrics:
        continue
        print("Stage: {} not found.".format(stage))
        return False

      for label in self.comparisons:
        self.results[stage][label] = self.comparisons[label](self.metrics[stage], self.features[stage])

      for label in self.extract_data:
        self.data[stage][label] = self.extract_data[label](self.metrics[stage], self.features[stage])

    return True

  def stages_sorted_by_factor(self):
    stages_and_factors = []
    for stage in self.results:
      factors = []
      for label in self.results[stage]:
        factors.append(abs(self.results[stage][label].factor))

      if len(factors):
        stages_and_factors.append((stage, max(factors)))
        stages_and_factors.sort(key=lambda s: s[1])

    return [s[0] for s in stages_and_factors]

  def stages_sorted_by_ratio(self):
    stages_and_ratios = []
    for stage in self.results:
      ratios = []
      for label in self.results[stage]:
        ratios.append(abs(self.results[stage][label].ratio))

      if len(ratios):
        stages_and_ratios.append((stage, max(ratios)))
        stages_and_ratios.sort(key=lambda s: s[1])

    return [s[0] for s in stages_and_ratios]

  def max_factor(self):
    factors = []
    for stage in self.results:
      if self.should_ignore(stage):
        continue

      for label in self.results[stage]:
        factors.append(abs(self.results[stage][label].factor))

    return max(factors)

  def max_ratio(self):
    ratios = []
    for stage in self.results:
      if self.should_ignore(stage):
        continue

      for label in self.results[stage]:
        ratios.append(abs(self.results[stage][label].ratio))

    return max(ratios)

  def __str__(self):
    out = "{}/autoschedule_command.txt\n".format(self.path.parent)
    first = True
    for stage in self.stages_sorted_by_factor():
      if self.should_ignore(stage):
        continue

      width = max([len(k.value) for k in self.comparisons.keys()])
      #data_width = max([len(k.value) for k in self.extract_data.keys()])
      #width = max(width, data_width)

      registers_64 = "?"
      registers_256 = "?"
      if Data.REGISTERS_64.value in self.metrics[stage]:
        registers_64 = self.metrics[stage][Data.REGISTERS_64.value]
      if Data.REGISTERS_256.value in self.metrics[stage]:
        registers_256 = self.metrics[stage][Data.REGISTERS_256.value]

      stage_str = "{} (Reg. = {}; {})".format(stage, registers_64, registers_256)

      if first:
        first = False
        out += "{:{width}} {:>14} {:>14} {:>7} {:>7}\n".format(stage_str, "Actual", "Predicted", "Ratio", "Factor", width=width + 2)
      else:
        out += "{:{width}}\n".format(stage_str, width=width + 2)

      for label in self.data[stage]:
        out += "  {:{width}} {}\n".format(label.value, str(self.data[stage][label]), width=width)

      for label in self.results[stage]:
        out += "  {:{width}} {}\n".format(label.value, str(self.results[stage][label]), width=width)

    return out

def compare_metrics_and_features(outliers_filename, N):
  with open(outliers_filename) as file:
    all_sample_paths = [Path(line.rstrip().split(", ")[0]) for line in file]

  sample_paths = all_sample_paths
  if N > 0:
    sample_paths = all_sample_paths[0:N]
    sample_paths.extend(all_sample_paths[-N:])

  samples = []

  for sample_path in sample_paths:
    metrics_path = Path("{}/formatted_metrics.txt".format(sample_path.parent))
    features_path = Path("{}/formatted_features.txt".format(sample_path.parent))

    if not metrics_path.is_file():
      print("Metrics file not found: {}".format(metrics_path))
      continue

    if not features_path.is_file():
      print("Features file not found: {}".format(features_path))
      continue

    sample = Sample(sample_path, metrics_path, features_path)
    if sample.success:
      samples.append(sample)
    else:
      print("Metrics failed: {}".format(sample_path))

  samples.sort(key=lambda s: s.max_factor())
  for s in samples:
    print(s)

if __name__ == "__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument("--outliers", type=str, required=True)
  parser.add_argument("--N", type=int, required=True)

  args = parser.parse_args()

  compare_metrics_and_features(Path(args.outliers), args.N)

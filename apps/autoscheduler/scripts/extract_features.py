import argparse
from pathlib import Path
import pdb
import sh
import os
from enum import Enum
import copy

class MemType(Enum):
  GLOBAL = "global"
  SHARED = "shared"
  LOCAL = "local"

class AccessType(Enum):
  LOAD = "load"
  STORE = "store"
  LOAD_AND_STORE = "load_and_store"

class MemAccessType(Enum):
  GLOBAL_MEM_LOAD = "global_mem_load"
  GLOBAL_MEM_STORE = "global_mem_store"
  GLOBAL_MEM_LOAD_STORE = "global_mem_load_and_store"
  SHARED_MEM_LOAD = "shared_mem_load"
  SHARED_MEM_STORE = "shared_mem_store"

class MemAccess:
  def __init__(self, mem_type, access_type, consumer, compute_root_stage, producer, num_requests_per_block, num_blocks, num_transactions_per_request, tail_num_requests_per_block, tail_num_transactions_per_request, all_coeffs_exist):
    self.mem_type = mem_type
    self.access_type = access_type
    self.consumer = consumer
    self.compute_root_stage = compute_root_stage
    self.num_requests = num_blocks * num_requests_per_block
    self.num_transactions = self.num_requests * num_transactions_per_request
    self.num_requests += num_blocks * tail_num_requests_per_block
    self.num_transactions += num_blocks * tail_num_requests_per_block * tail_num_transactions_per_request
    self.all_coeffs_exist = all_coeffs_exist
    self.key = "{}_{}_{}".format(compute_root_stage, mem_type.value, access_type.value)

  def add(self, other):
    assert(self.compute_root_stage == other.compute_root_stage)
    assert(self.mem_type == other.mem_type)
    assert(self.access_type == other.access_type)
    assert(self.key == other.key)

    self.num_requests += other.num_requests
    self.num_transactions += other.num_transactions
    self.all_coeffs_exist = self.all_coeffs_exist and other.all_coeffs_exist

  def __str__(self):
    prefix = "{} num_{}_{}".format(
      self.compute_root_stage,
      self.mem_type.value,
      self.access_type.value,
    )

    num_requests = "{}_requests {}".format(
      prefix,
      self.num_requests,
    )

    num_transactions_per_request = "{}_transactions_per_request {:.2f}".format(
      prefix,
      self.transactions_per_request(),
    )

    all_coeffs_exist = "{} {}_{}_all_coeffs_exist {}".format(
      self.compute_root_stage,
      self.mem_type.value,
      self.access_type.value,
      int(self.all_coeffs_exist),
    )

    return "{}\n{}\n{}\n".format(
      num_requests,
      num_transactions_per_request,
      all_coeffs_exist
    )

  def transactions_per_request(self):
    return self.num_transactions / self.num_requests if self.num_requests != 0 else 0

  @staticmethod
  def create(mem_access):
    expected_keys = [
      "consumer",
      "compute_root_stage",
      "mem_type",
      "access_type",
      "num_blocks",
      "num_requests_per_block",
      "num_transactions_per_request",
      "tail_num_requests_per_block",
      "tail_num_transactions_per_request",
      "all_coeffs_exist",
    ]

    for k in expected_keys:
      if "tail_num_requests_per_block" not in mem_access:
        mem_access["tail_num_requests_per_block"] = 0
      if "tail_num_transactions_per_request" not in mem_access:
        mem_access["tail_num_transactions_per_request"] = 0

      if k not in mem_access:
        continue
        pdb.set_trace()
      assert(k in mem_access)

    return MemAccess(**mem_access)


def add_access(global_mem_loads, global_mem_stores, in_load, consumer, num_requests_per_block, num_blocks, num_transactions_per_request, all_coeffs_exist):
  access = GlobalMemAccess(in_load, consumer, num_requests_per_block, num_blocks, num_transactions_per_request, all_coeffs_exist)

  table = global_mem_loads if in_load else global_mem_stores
  if consumer in table:
    table[consumer].add(access)
  else:
    table[consumer] = access

class FeatureParser:
  def __init__(self, filename):
    self.filename = filename
    self.formatted_filename = "{}/formatted_features.txt".format(Path(self.filename).parent)
    self.mem_accesses = {}
    self.features = {}
    self.stages = self.get_stages()
    self.in_mem_access = False

    self.mem_access_data = [
      "num_blocks",
      "num_transactions_per_request",
      "num_requests_per_block",
      "tail_num_requests_per_block",
      "tail_num_transactions_per_request",
    ]

    self.feature_names = set([
      "num_blocks",
      "num_shared_mem_loads_per_block",
      "num_global_mem_loads_per_block",
      "num_local_mem_loads_per_thread",
      "num_shared_mem_stores_per_block",
      "num_global_mem_stores_per_block",
      "num_local_mem_stores_per_thread",
      "global_mem_store_efficiency",
      "global_mem_load_efficiency",
    ])

  def is_in_mem_access(self):
    return self.in_mem_access

  def parse_mem_type(self, access_type):
    options = [
      MemType.GLOBAL,
      MemType.SHARED,
      MemType.LOCAL,
    ]

    for op in options:
      if access_type.startswith(op.value):
        return op

    assert(False)

  def parse_access_type(self, access_type):
    options = [
      AccessType.LOAD,
      AccessType.STORE,
      AccessType.LOAD_AND_STORE,
    ]

    for op in options:
      if access_type.endswith("mem_" + op.value):
        return op

    assert(False)

  def parse_begin_mem_access(self, line):
    line = line.split()
    access_type = line[3][:-1]
    consumer = line[5][:-1]

    return {
      "mem_type": self.parse_mem_type(access_type),
      "access_type": self.parse_access_type(access_type),
      "consumer": consumer,
      "compute_root_stage": self.stages[consumer],
      "producer": line[7],
    }

  def parse_mem_access_data(self, line, mem_access):
    for data in self.mem_access_data:
      if line.startswith(data):
        mem_access[data] = int(line.split()[2])

  def parse_end_mem_access(self, line, mem_access):
    mem_access["all_coeffs_exist"] = not line.endswith("(not all coeffs exist)")

  def add_mem_access(self, mem_access):
    to_add = []

    # If the access is a LOAD_AND_STORE, convert to 2 separate accesses: 1 load
    # and 1 store
    if mem_access["access_type"] == AccessType.LOAD_AND_STORE:
      load = copy.deepcopy(mem_access)
      load["access_type"] = AccessType.LOAD
      mem_access["access_type"] = AccessType.STORE
      to_add.append(load)

    to_add.append(mem_access)

    for a in to_add:
      if "num_requests_per_block" not in a:
        continue
      access = MemAccess.create(a)
      if access.key in self.mem_accesses:
        self.mem_accesses[access.key].add(access)
      else:
        self.mem_accesses[access.key] = access

  def parse_feature(self, stage, line):
    line = line.split()
    feature = line[0][:-1]

    if not feature in self.feature_names:
      return

    if not stage in self.features:
      self.features[stage] = {}

    self.features[stage][feature] = float(line[1])

  def process_features(self):
    processed_features = {}
    for stage in self.features:
      num_blocks = int(self.features[stage]["num_blocks"])
      num_global_stores_per_block = int(self.features[stage]["num_global_mem_stores_per_block"])
      num_global_loads_per_block = int(self.features[stage]["num_global_mem_loads_per_block"])
      num_shared_stores_per_block = int(self.features[stage]["num_shared_mem_stores_per_block"])
      num_shared_loads_per_block = int(self.features[stage]["num_shared_mem_loads_per_block"])

      global_mem_load_efficiency = float(self.features[stage]["global_mem_load_efficiency"])
      global_mem_store_efficiency = float(self.features[stage]["global_mem_store_efficiency"])

      num_global_stores = num_blocks * num_global_stores_per_block
      num_global_loads = num_blocks * num_global_loads_per_block
      num_shared_stores = num_blocks * num_shared_stores_per_block
      num_shared_loads = num_blocks * num_shared_loads_per_block

      if num_global_loads == 0 and num_global_stores == 0 and num_shared_loads == 0 and num_shared_stores == 0:
        continue

      global_mem_load_transactions_used = global_mem_load_efficiency * num_global_loads
      global_mem_store_transactions_used = global_mem_store_efficiency * num_global_stores

      compute_root_stage = self.stages[stage]
      if not compute_root_stage in processed_features:
        processed_features[compute_root_stage] = {
          "num_blocks": num_blocks,
          "num_global_mem_store_transactions": 0,
          "num_global_mem_load_transactions": 0,
          "num_shared_mem_store_transactions": 0,
          "num_shared_mem_load_transactions": 0,
          "num_global_mem_load_transactions_used": 0,
          "num_global_mem_store_transactions_used": 0,
        }

      processed_features[compute_root_stage]["num_global_mem_store_transactions"] += num_global_stores
      processed_features[compute_root_stage]["num_global_mem_load_transactions"] += num_global_loads
      processed_features[compute_root_stage]["num_shared_mem_store_transactions"] += num_shared_stores
      processed_features[compute_root_stage]["num_shared_mem_load_transactions"] += num_shared_loads
      processed_features[compute_root_stage]["num_global_mem_load_transactions_used"] += global_mem_load_transactions_used
      processed_features[compute_root_stage]["num_global_mem_store_transactions_used"] += global_mem_store_transactions_used

    for stage in processed_features:
      transactions_used = processed_features[stage]["num_global_mem_load_transactions_used"]
      transactions = processed_features[stage]["num_global_mem_load_transactions"]

      processed_features[stage]["global_mem_load_efficiency"] = 1
      if transactions > 0:
        processed_features[stage]["global_mem_load_efficiency"] = transactions_used / transactions

      transactions_used = processed_features[stage]["num_global_mem_store_transactions_used"]
      transactions = processed_features[stage]["num_global_mem_store_transactions"]
      processed_features[stage]["global_mem_store_efficiency"] = 1
      if transactions > 0:
        processed_features[stage]["global_mem_store_efficiency"] = transactions_used / transactions

      processed_features[stage].pop("num_global_mem_load_transactions_used")
      processed_features[stage].pop("num_global_mem_store_transactions_used")

    return processed_features

  def parse(self):
    mem_access = {}
    in_features = False
    feature_stage = None

    with open(self.filename) as file:
      for line in file:
        if line.startswith("BEGIN MEM ACCESS"):
          mem_access = self.parse_begin_mem_access(line)
        elif line.startswith("END MEM ACCESS"):
          self.parse_end_mem_access(line, mem_access)

          self.add_mem_access(mem_access)
          mem_access = {}
        elif mem_access:
          self.parse_mem_access_data(line, mem_access)
        elif line.startswith("Schedule features for"):
          feature_stage = line.split()[3]
          in_features = True
        elif line.startswith("State with cost"):
          in_features = False
          feature_stage = None
        elif in_features:
          self.parse_feature(feature_stage, line)
        elif line.startswith("BEGIN Final generated loop nest"):
          in_loop_nest = True

  def write_to_file(self):
    processed_features = self.process_features()

    with open(self.formatted_filename, "w+") as file:
      for key in self.mem_accesses:
        file.write(str(self.mem_accesses[key]))

      for stage in processed_features:
        for feature in processed_features[stage]:
          file.write("{} {} {}\n".format(stage, feature, processed_features[stage][feature]))

    print("Formatted features saved to {}".format(self.formatted_filename))

  def get_stages(self):
    stages = {}
    in_compute_locations = False

    with open(self.filename) as file:
      for line in file:
        if line.startswith("BEGIN compute locations"):
          in_compute_locations = True
          continue
        elif line.startswith("END compute locations"):
          break
        elif in_compute_locations:
          line = line.split()
          compute_root = line[0]
          for stage in line[2:]:
            stages[stage] = compute_root

    return stages


def extract_features(filename):
  parser = FeatureParser(filename)
  parser.parse()
  parser.write_to_file()

if __name__ == "__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument("--filename", type=str, required=True)

  args = parser.parse_args()

  extract_features(Path(args.filename))

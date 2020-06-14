import argparse
from pathlib import Path
import pdb
import sh
import os
import math
from enum import Enum
import re

class GlobalMemLoad:
  def __init__(self, consumer, num_requests_per_block, num_blocks, num_transactions_per_request):
    assert(consumer is not None)
    assert(num_requests_per_block is not None)
    assert(num_blocks is not None)
    assert(num_transactions_per_request is not None)

    self.consumer = consumer
    self.num_requests = num_blocks * num_requests_per_block
    self.num_transactions = self.num_requests * num_transactions_per_request

  def add(self, other):
    assert(self.consumer == other.consumer)
    self.num_requests += other.num_requests
    self.num_transactions += other.num_transactions

  def __str__(self):
    return "{} num_global_load_requests {}\n{} num_global_load_transactions_per_request {:.2f}\n".format(
      self.consumer,
      self.num_requests,
      self.consumer,
      self.num_transactions / self.num_requests if self.num_requests != 0 else 0,
    )

def get_stages(filename):
  stages = set()

  with open(filename) as file:
    for line in file:
      if not line.startswith("Schedule features for"):
        continue

      stages.add(line.split()[3])

  return stages

def extract_features(filename):
  stages = get_stages(filename)

  global_mem_loads = {}
  in_global_mem_load = False

  consumer = None
  num_blocks = None
  num_transactions_per_request = None
  num_requests_per_warp = None
  num_warps = None

  with open(filename) as file:
    for line in file:
      if line.startswith("BEGIN global_mem_load. consumer:"):
        in_global_mem_load = True
        consumer = line.split()[3][:-1]
        producer = line.split()[5]
      elif in_global_mem_load and line.startswith("num_blocks"):
        num_blocks = int(line.split()[2])
      elif in_global_mem_load and line.startswith("num_transactions_per_request"):
        num_transactions_per_request = int(line.split()[2])
      elif in_global_mem_load and line.startswith("num_requests"):
        num_requests_per_block = int(line.split()[2])
      elif line.startswith("END global_mem_load. consumer:"):
        in_global_mem_load = False
        load = GlobalMemLoad(consumer, num_requests_per_block, num_blocks, num_transactions_per_request)

        if consumer in global_mem_loads:
          global_mem_loads[consumer].add(load)
        else:
          global_mem_loads[consumer] = load
        consumer = None
        num_blocks = None
        num_transactions_per_request = None
        num_requests_per_warp = None
        num_warps = None

  for stage in stages:
    if stage in global_mem_loads:
      continue

    global_mem_loads[stage] = GlobalMemLoad(stage, 0, 0, 0)

  assert(not in_global_mem_load)
  for consumer in global_mem_loads:
    with open("{}/formatted_features.txt".format(Path(filename).parent), "a") as file:
      file.write(str(global_mem_loads[consumer]))

if __name__ == "__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument("--filename", type=str, required=True)

  args = parser.parse_args()

  extract_features(Path(args.filename))

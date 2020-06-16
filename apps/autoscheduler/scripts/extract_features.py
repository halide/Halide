import argparse
from pathlib import Path
import pdb
import sh
import os
import math
from enum import Enum
import re

class GlobalMemAccess:
  def __init__(self, is_load, consumer, num_requests_per_block, num_blocks, num_transactions_per_request, all_coeffs_exist):
    assert(consumer is not None)
    assert(num_requests_per_block is not None)
    assert(num_blocks is not None)
    assert(num_transactions_per_request is not None)

    self.is_load = is_load
    self.consumer = consumer
    self.num_requests = num_blocks * num_requests_per_block
    self.num_transactions = self.num_requests * num_transactions_per_request
    self.all_coeffs_exist = all_coeffs_exist

  def add(self, other):
    assert(self.consumer == other.consumer)
    assert(self.is_load == other.is_load)
    self.num_requests += other.num_requests
    self.num_transactions += other.num_transactions
    self.all_coeffs_exist = self.all_coeffs_exist and other.all_coeffs_exist

  def __str__(self):
    access_type = "load" if self.is_load else "store"
    return "{} num_global_{}_requests {}\n{} num_global_{}_transactions_per_request {:.2f}\n{} all_coeffs_exist {}\n".format(
      self.consumer,
      access_type,
      self.num_requests,
      self.consumer,
      access_type,
      self.num_transactions / self.num_requests if self.num_requests != 0 else 0,
      self.consumer,
      int(self.all_coeffs_exist)
    )

def get_stages(filename):
  stages = set()

  with open(filename) as file:
    for line in file:
      if not line.startswith("Schedule features for"):
        continue

      stages.add(line.split()[3])

  return stages

def add_access(global_mem_loads, global_mem_stores, in_load, consumer, num_requests_per_block, num_blocks, num_transactions_per_request, all_coeffs_exist):
  access = GlobalMemAccess(in_load, consumer, num_requests_per_block, num_blocks, num_transactions_per_request, all_coeffs_exist)

  table = global_mem_loads if in_load else global_mem_stores
  if consumer in table:
    table[consumer].add(access)
  else:
    table[consumer] = access

def extract_features(filename):
  stages = get_stages(filename)

  global_mem_loads = {}
  global_mem_stores = {}
  in_mem_access = False

  consumer = None
  num_blocks = None
  num_transactions_per_request = None
  num_requests_per_warp = None
  num_warps = None

  tail_warp_num_transactions_per_request = None
  tail_warp_num_requests_per_block = None

  with open(filename) as file:
    for line in file:
      if line.startswith("BEGIN GLOBAL ACCESS"):
        in_load = line.startswith("BEGIN GLOBAL ACCESS global_mem_load.") or line.startswith("BEGIN GLOBAL ACCESS global_mem_load_and_store.")
        in_store = line.startswith("BEGIN GLOBAL ACCESS global_mem_store.") or line.startswith("BEGIN GLOBAL ACCESS global_mem_load_and_store.")
        in_mem_access = True
        consumer = line.split()[5][:-1]
        producer = line.split()[7]
      elif in_mem_access and line.startswith("num_blocks"):
        num_blocks = int(line.split()[2])
      elif in_mem_access and line.startswith("num_transactions_per_request"):
        num_transactions_per_request = int(line.split()[2])
      elif in_mem_access and line.startswith("num_requests_per_block"):
        num_requests_per_block = int(line.split()[2])
      elif in_mem_access and line.startswith("tail_warp_num_requests_per_block"):
        tail_warp_num_requests_per_block = int(line.split()[2])
      elif in_mem_access and line.startswith("tail_warp_num_transactions_per_request"):
        tail_warp_num_transactions_per_request = int(line.split()[2])
      elif line.startswith("END GLOBAL ACCESS"):
        all_coeffs_exist = True
        if line.endswith("(not all coeffs exist)"):
          all_coeffs_exist = False

        if in_load:
          add_access(global_mem_loads, global_mem_stores, True, consumer, num_requests_per_block, num_blocks, num_transactions_per_request, all_coeffs_exist)

          if tail_warp_num_requests_per_block is not None and tail_warp_num_transactions_per_request is not None:
            add_access(global_mem_loads, global_mem_stores, True, consumer, tail_warp_num_requests_per_block, num_blocks, tail_warpnum_transactions_per_request, all_coeffs_exist)

        if in_store:
          add_access(global_mem_loads, global_mem_stores, False, consumer, num_requests_per_block, num_blocks, num_transactions_per_request, all_coeffs_exist)

          if tail_warp_num_requests_per_block is not None and tail_warp_num_transactions_per_request is not None:
            add_access(global_mem_loads, global_mem_stores, False, consumer, tail_warp_num_requests_per_block, num_blocks, tail_warpnum_transactions_per_request, all_coeffs_exist)

        in_load = False
        in_store = False
        in_mem_access = False
        consumer = None
        num_blocks = None
        num_transactions_per_request = None
        num_requests_per_warp = None
        num_warps = None
        tail_warp_num_transactions_per_request = None
        tail_warp_num_requests_per_warp = None

  for stage in stages:
    if not stage in global_mem_loads:
      global_mem_loads[stage] = GlobalMemAccess(True, stage, 0, 0, 0, 1)

    if not stage in global_mem_stores:
      global_mem_stores[stage] = GlobalMemAccess(False, stage, 0, 0, 0, 1)

  assert(not in_mem_access)
  for consumer in global_mem_loads:
    with open("{}/formatted_features.txt".format(Path(filename).parent), "a") as file:
      file.write(str(global_mem_loads[consumer]))

  for consumer in global_mem_stores:
    with open("{}/formatted_features.txt".format(Path(filename).parent), "a") as file:
      file.write(str(global_mem_stores[consumer]))

if __name__ == "__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument("--filename", type=str, required=True)

  args = parser.parse_args()

  extract_features(Path(args.filename))

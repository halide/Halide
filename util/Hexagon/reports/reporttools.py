import fileinput
import re
from subprocess import Popen, PIPE
import json
import sys, getopt
from uuid import uuid4 as uuid

def children(node):
    return node.get("loops", node.get("forks", {}))

def flatten_profile(call_tree, merge_threads = False, debug = False):
    flat_profile = {};

    def flatten(node):
        name = node["name"]
        thread_id = node["thread_id"] if not merge_threads else 0

        if (name, thread_id) not in flat_profile:
            flat_profile[(name, thread_id)] = {
                "name": name,
                "thread_id": thread_id,
                "times_called": 0,
                "total_time_ns": 0,
                "overhead_time_ns": 0,
                "self_time_ns": 0,
                "root_relatime": 0,
                "self_root_relatime": 0
            }

        profile = flat_profile[(name, thread_id)]

        profile["times_called"] += node["times_called"]
        profile["total_time_ns"] += node["total_time_ns"]
        profile["overhead_time_ns"] += node["overhead_time_ns"]
        profile["self_time_ns"] += node["self_time_ns"]
        profile["root_relatime"] += node["root_relatime"]
        profile["self_root_relatime"] += node["self_root_relatime"]

        for child in children(node).values():
            flatten(child)

    def finalize():
        for profile in flat_profile.values():
            n = profile["times_called"]
            t_t = profile["total_time_ns"]
            t_s = profile["self_time_ns"]
            profile["mean_self_time_ns"] = int(t_s / n if n > 0 else 0)
            profile["mean_total_time_ns"] = int(t_t / n if n > 0 else 0)
            if t_t > 0:
                profile["self_relatime"] = t_s / t_t
            else:
                profile["self_relatime"] = None

    flatten(call_tree)
    finalize()

    if debug:
        total_self_relatime = 0
        for profile in flat_profile.values():
            total_self_relatime += profile["self_root_relatime"]
        if total_self_relatime != 1:
            raise Exception("failed sum(self_root_relatime) == 1 sanity check")

    return list(flat_profile.values())

def flatten_profile_unique(call_tree):
    flat_profile = [];

    def flatten(node):
        n = node.copy()
        n.pop("loops", None)
        n.pop("forks", None)
        flat_profile.append(n)
        
        for child in children(node).values():
            flatten(child)

    flatten(call_tree)

    return flat_profile

def append_node_uuids(call_tree):
    call_tree["uuid"] = str(uuid())
    for child in children(call_tree).values():
        child = append_node_uuids(child);

#!/usr/bin/python

import fileinput
import re
from subprocess import Popen, PIPE
import json
import colorsys
import math
import sys
import argparse
from reporttools import flatten_profile

def main():
    parser = argparse.ArgumentParser(
            description='Summarize Hexagon profiling JSON data by flattening the tree and merging nodes'
            )
    parser.add_argument('-p', '--precision'
            , help='Display precision of floating-point numbers'
            , type=int , default=4)
    parser.add_argument('-o', '--output'
            , help='Output format (default json)'
            , type=str
            , choices=['json', 'csv']
            , default='json')
    parser.add_argument('-v', '--verbose'
            , help='Debugging output'
            , action='store_true')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--thread'
            , help='Display stats for specific thread ("main" is an alias for the main thread)'
            , type=str)
    group.add_argument('--cputime'
            , help='Display total cpu time instead of wall clock time (some nodes may display over 100%%)'
            , action='store_true')
    args = parser.parse_args()

    if args.cputime:
        args.thread = "merge"

    profile = json.load(sys.stdin)

    def call_tree(thread):
        thread_table = dict(profile["thread_table"])
        main_thread = thread_table[str(profile["call_tree"]["thread_id"])]

        if thread is None or thread == "merge":
            return profile["call_tree"]
        elif thread == "main":
            return profile["call_trees_by_thread"][main_thread['name']]
        else:
            try:
                if int(thread) in thread_table:
                    thread = thread_table[int(thread)]
            except ValueError:
                pass

            if thread in profile["call_trees_by_thread"]:
                return profile["call_trees_by_thread"][thread]

            raise Exception(f"no such thread: {thread}")

    report = flatten_profile(call_tree(args.thread), args.thread == "merge", args.verbose)

    if args.output == "json":
        print(json.dumps(report));
    elif args.output == "csv":
        keys = ["name",
                "total_time_ns",
                "self_time_ns",
                "self_relatime",
                "times_called",
                "mean_self_time_ns",
                "mean_total_time_ns",
                "root_relatime",
                "self_root_relatime",
                "overhead_time_ns"]
        print(','.join(keys))
        for profile in report:
            row = []
            for column in map(lambda k: profile[k], keys):
                if type(column) is float:
                    column = round(column, args.precision)
                elif column is None:
                    column = "--"
                row.append(str(column))
            print(','.join(row))

if __name__ == "__main__":
    main()

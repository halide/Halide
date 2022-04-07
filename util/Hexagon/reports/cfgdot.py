#!/usr/bin/python

import fileinput
import re
from subprocess import Popen, PIPE
import json
import colorsys
import math
import sys
import argparse
from reporttools import flatten_profile, flatten_profile_unique, append_node_uuids, children

def report_dot(profile, collapsing_common_nodes = False):
    header = "strict digraph g {"
    footer = "}"

    lines = [header]

    flat_profile = []

    call_tree = profile["call_tree"]
    thread_table = dict(profile["thread_table"])

    if collapsing_common_nodes:
        flat_profile = flatten_profile(call_tree)
    else:
        append_node_uuids(call_tree)
        flat_profile = flatten_profile_unique(call_tree)

    def get_node_id(node):
        return node.get("uuid", node["name"] + "." + str(node["thread_id"]))

    def report_dot_recursive(subgraph, parent=None):
        new_parent = get_node_id(subgraph)
        if parent is not None:
            lines.append('"' + parent + '"' + " -> " + '"' + new_parent + '"')
        for branch in (children(subgraph)).values():
            report_dot_recursive(branch, new_parent)

    report_dot_recursive(call_tree)

    flat_profile_by_thread_id = {}
    for report in flat_profile:
        thread_id = report["thread_id"]
        if thread_id not in flat_profile_by_thread_id:
            flat_profile_by_thread_id[thread_id] = []
        flat_profile_by_thread_id[thread_id].append(report)
    
    def add_node(report):
        name = report["name"]
        thread_id = report["thread_id"]
        total_time_ns = int(report["total_time_ns"])
        self_time_ns = int(report["self_time_ns"])
        overhead_time_ns = int(report["overhead_time_ns"])
        times_called = int(report["times_called"])
        root_relatime = report["root_relatime"]
        calls_plural = '' if times_called == 1 else 's'
        mean_total_time_ns = report["mean_total_time_ns"]
        mean_self_time_ns = report["mean_self_time_ns"]
        root_relatime = round(100*report["root_relatime"], 2)
        self_root_relatime = round(100*report["self_root_relatime"], 2)
        rows = [
            [f'{name}'],
            [f'{total_time_ns}ns', 'total time'],
            [f'{self_time_ns}ns', 'self time']]

        if overhead_time_ns > 0:
            rows.append([f'{overhead_time_ns}ns', 'profiling overhead'])

        if report.get("self_relatime"):
            self_relatime = round(100*report["self_relatime"], 2)
            rows.append([f'{self_relatime}%', 'of total (self)'])

        rows += [
            [f'{times_called}', f'call{calls_plural}'],
            [f'{mean_total_time_ns}ns', 'per call (total)'],
            [f'{mean_self_time_ns}ns', 'per call (self)'],
        ]

        if report.get("parent_relatime"):
            parent_relatime = round(100*report["parent_relatime"], 2)
            rows += [ [f'{parent_relatime}%', 'of parent (total)'] ]
            if report.get("self_parent_relatime"):
                self_parent_relatime = round(100*report["self_parent_relatime"], 2)
                rows += [ [f'{self_parent_relatime}%', 'of parent (self)'] ]

        rows += [ [f'{root_relatime}%', 'of runtime (total)'],
                  [f'{self_root_relatime}%', 'of runtime (self)'] ]

        html_label = '<table>'
        for row in rows:
            html_label += "<tr>"
            for cell in row:
                html_label += "<td>" + cell + "</td>"
            html_label += "</tr>"
        html_label += '</table>'

        color = 'white'
        if "self_root_relatime" in report:
            p = report["self_root_relatime"]

            h_range = [0.2, 0.0, 0.0]
            s_range = [0.0, 0.4, 0.5, 0.6]
            v_range = [1.0]

            def coord(r):
                t = p * (len(r)-1)
                t0 = int(math.floor(t))
                t1 = min(len(r)-1, t0+1)

                a = r[t0]
                b = r[t1]
                x = t - t0
                return a*(1-x) + b*x

            h = coord(h_range)
            s = coord(s_range)
            v = coord(v_range)
            (r,g,b) = colorsys.hsv_to_rgb(h,s,v)

            def to_hex(component):
                return re.sub("0x", "", hex(int(component*255))).zfill(2)

            color = "#" + to_hex(r) + to_hex(g) + to_hex(b)


        uuid = get_node_id(report)
        lines.append(f'"{uuid}" [shape=box, label=<{html_label}>, style=filled, fillcolor="{color}"]')

    i = 0
    for thread_id, reports in flat_profile_by_thread_id.items():
        lines.append(f'subgraph cluster_{thread_id}' + '{')
        lines.append(f'label="{thread_table[str(thread_id)]["name"]}"')
        color = 'blue' if i % 2 else 'dimgrey'
        lines.append(f'color={color}')
        lines.append(f'fontcolor={color}')
        i += 1
        for report in reports:
            add_node(report)
        lines.append('}')

    lines.append(footer)

    return '\n'.join(lines)

def main():
    parser = argparse.ArgumentParser(
            description='Convert Hexagon profiling JSON data into Graphviz dotfile'
            )
    parser.add_argument('--expand'
            , help='Do not merge nodes that share a name and thread (i.e. emit a tree instead of a directed acyclic graph)'
            , action='store_true')
    args = parser.parse_args()

    profile = json.load(sys.stdin)

    print(report_dot(profile, not args.expand))

if __name__ == "__main__":
    main()

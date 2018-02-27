from os import listdir
from os.path import isfile, join
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

'''
// MachineParams: 16,16777216,40,-1,-1
Total: 4953365632, arith: 3401708928, mem: 1551656704
# of groups: 8
# inline fusion: 0
# fast-mem fusion: 0
# total fusion: 0
Manually-tuned time: 4.82323ms
Auto-scheduled time: 5.63013ms
'''
def parse_balance(line):
    # // MachineParams: 16,16777216,40,-1,-1
    machine_params = line.split(": ")
    machine_params = machine_params[1].split(",")
    balance = int(machine_params[2])
    return balance

def parse_cost(line):
    # Total: 4953365632, arith: 3401708928, mem: 1551656704
    costs = line.split(", ")
    costs = costs[0].split(": ")
    total = int(costs[1])
    return total

def parse_runtime(line):
    # Auto-scheduled time: 5.63013ms
    runtime = line.split(": ")
    runtime = runtime[1].split("ms")
    runtime = float(runtime[0])
    return runtime

def parse_benchmark(benchmark_dir, filename):
    f = open(benchmark_dir + filename, 'r')
    lines = f.read().split('\n')
    f.close()
    # Parse the benchmark data
    balance_benchmark, cost_benchmark, runtime_benchmark = [], [], []
    for line in lines:
        if "MachineParams" in line:
            balance_benchmark.append(parse_balance(line))
        elif "Total:" in line:
            cost_benchmark.append(parse_cost(line))
        elif "Auto-scheduled time:" in line:
            runtime_benchmark.append(parse_runtime(line))
    assert(len(balance_benchmark) == len(cost_benchmark))
    assert(len(balance_benchmark) == len(runtime_benchmark))
    return balance_benchmark, cost_benchmark, runtime_benchmark

def analyze_benchmarks(benchmark_dir, benchmark_files):
    balance_benchmarks, cost_benchmarks, runtime_benchmarks = {}, {}, {}
    for filename in benchmark_files:
        test_name = filename.split(".txt")[0]
        balance_benchmark, cost_benchmark, runtime_benchmark = parse_benchmark(benchmark_dir, filename)
        balance_benchmarks[test_name] = balance_benchmark
        cost_benchmarks[test_name] = cost_benchmark
        runtime_benchmarks[test_name] = runtime_benchmark
    return balance_benchmarks, cost_benchmarks, runtime_benchmarks

def split_per_balance(balance, cost, runtime):
    assert(len(balance) == len(cost))
    assert(len(balance) == len(runtime))
    cost_benchmarks, runtime_benchmarks = {}, {}
    for i in range(len(balance)):
        if balance[i] not in cost_benchmarks:
            cost_benchmarks[balance[i]] = []
            runtime_benchmarks[balance[i]] = []
        cost_benchmarks[balance[i]].append(cost[i])
        runtime_benchmarks[balance[i]].append(runtime[i])
    return cost_benchmarks, runtime_benchmarks

def plot_data(filename, cost, runtime, title, normalize_cost=True, show=False):
    if normalize_cost:
        min_cost = min(cost)
        assert(min_cost > 0)
        cost = [float(c)/min_cost for c in cost]
    plt.plot(cost, runtime)
    plt.plot(cost, runtime, 'bo', cost, runtime, 'k')
    plt.title(title)
    if normalize_cost:
        plt.xlabel("Normalized cost")
    else:
        plt.xlabel("Cost")
    plt.ylabel("Runtime (ms)")
    plt.grid()
    if show:
        plt.show()
    plt.savefig(filename, dpi = 100)   # save the figure to file
    plt.clf()

# Write per-test benchmark to file
def convert_to_table(filename, title, balance, cost, runtime):
    # Create the dataframe
    raw_data = {'Balance': balance, 'Cost': cost, 'Runtime (ms)': runtime}
    df = pd.DataFrame(raw_data, columns = ['Balance', 'Cost', 'Runtime (ms)'])
    df.to_csv(filename, encoding='utf-8', index=False)
    with open(filename, "r+") as f:
        lines = f.readlines() # read old content
        f.seek(0)             # go back to the beginning of the file
        f.write(title + "\n") # write new content at the beginning
        for line in lines:    # write old content after new
            f.write(line)
        f.close()
    return df

benchmark_dir = "./apps/benchmark_data/"
plot_dir = "./apps/benchmark_plot/"
csv_dir = "./apps/benchmark_csv/"
benchmark_files = [f for f in listdir(benchmark_dir) if isfile(join(benchmark_dir, f))]
benchmark_files = [f for f in benchmark_files if ".txt" in f]
balance_benchmarks, cost_benchmarks, runtime_benchmarks = analyze_benchmarks(benchmark_dir, benchmark_files)

import os, os.path
for d in [benchmark_dir, plot_dir, csv_dir]:
    if not os.path.isdir(d): os.mkdir(d)

if __name__ == '__main__':
    # Generate the csv data
    for test_name in balance_benchmarks:
        convert_to_table(csv_dir + test_name + ".csv", test_name, balance_benchmarks[test_name], cost_benchmarks[test_name], runtime_benchmarks[test_name])

    # Generate the plots per balance value
    for test_name in balance_benchmarks:
        cost_per_balance, runtime_per_balance = split_per_balance(balance_benchmarks[test_name], cost_benchmarks[test_name], runtime_benchmarks[test_name])
        for b in cost_per_balance:
            print("Balance: ", b, ", Cost: ", cost_per_balance[b], ", Runtime: ", runtime_per_balance[b])
            plot_data(plot_dir + test_name + "_balance_" + str(b) + ".png", cost_per_balance[b], runtime_per_balance[b], test_name + " (Balance " + str(b) + ")")



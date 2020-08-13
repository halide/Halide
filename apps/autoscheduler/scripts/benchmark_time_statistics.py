import argparse
from pathlib import Path
import pdb
import os
import numpy as np
import matplotlib
matplotlib.use("Agg")
matplotlib.rcParams["agg.path.chunksize"] = 10000
import matplotlib.pyplot as plt

def benchmark_time_statistics(timestamp):
  apps_dir = Path(__file__).absolute().parent.parent.parent
  apps = ["bgu", "bilateral_grid", "local_laplacian", "nl_means", "lens_blur", "camera_pipe", "stencil_chain", "harris",
          "hist", "max_filter", "unsharp", "interpolate_generator", "conv_layer", "cuda_mat_mul", "iir_blur_generator",
          "depthwise_separable_conv", "mobilenet0", "mobilenet1", "mobilenet2", "mobilenet3", "mobilenet4",
          "mobilenet5", "mobilenet6", "mobilenet7"]

  times = []

  for app in apps:
    path = Path("{}/{}/autotuned_samples-{}".format(apps_dir, app, timestamp))
    for root, dirs, filenames in os.walk(path):
      for filename in filenames:
        if not filename == "bench.txt":
          continue

        with open(os.path.join(root, filename)) as file:
          line = file.readline()
          if "Total benchmark time" in line:
            times.append(float(line.split()[20]))

  times = np.array(times)
  print("Found {} samples".format(len(times)))
  print("Mean benchmark time = {:.2f} s".format(np.mean(times)))
  print("Median benchmark time = {:.2f} s".format(np.median(times)))
  print("Max benchmark time = {:.2f} s".format(np.max(times)))
  print("Min benchmark time = {:.2f} s".format(np.min(times)))
  print("Variance = {:.2f} s".format(np.var(times)))
  print("Standard deviation = {:.2f} s".format(np.std(times)))
  gt_one = np.sum(times > 1)
  print("Num samples with benchmark time > 1 s = {:d} = {:.2f}%".format(gt_one, gt_one / len(times)))

  plt.hist(times, bins=100)
  plt.title("Benchmark Time Per Sample");
  plt.xlabel("Benchmark Time (s)");
  plt.savefig("benchmark_time_histogram.png", dpi=300)

if __name__ == "__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument("--timestamp", type=str, required=True)

  args = parser.parse_args()

  benchmark_time_statistics(args.timestamp)

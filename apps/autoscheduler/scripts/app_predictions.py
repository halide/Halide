from scipy import stats
import pdb
import argparse
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
matplotlib.rcParams["agg.path.chunksize"] = 10000
import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter
from matplotlib.ticker import FormatStrFormatter
from rsquared import rsquared, relative_loss
from path import Path

APP = 0
ID = 1
PREDICTED = 2
ACTUAL = 3
BYTES = 4

def sortbybytes(samples, N):
  samples.sort(key=lambda tup: tup[BYTES])
  i = 1
  candidates = []
  for s in samples:
    candidates.append(s)

    if i >= N:
      break
    i += 1

  return candidates

def bestofN(app, samples, N):
  if app == "lens_blur":
    return [("?", "?", "?", "?")]
    samples = sortbybytes(samples, 10)
    for s in samples:
      print("{},{},{},{}".format(s[ID], s[PREDICTED], s[ACTUAL], s[BYTES]))

  samples.sort(key=lambda tup: tup[PREDICTED])
  i = 1
  candidates = []
  for s in samples:
    candidates.append(s)

    if i >= N:
      break
    i += 1

  candidates.sort(key=lambda tup: tup[ACTUAL])
  return candidates

def predictions(app, filename):
  apps = [app]

  results = {}
  beam_search = {}

  with open(Path(filename)) as file:
    for line in file:
      parts = line.split(",")
      app = parts[APP].strip()
      id = int(parts[ID].strip())
      try:
        predicted = float(parts[PREDICTED].strip())
        actual = float(parts[ACTUAL].strip()) * 1000
      except:
        continue
      bytes = int(parts[BYTES].strip())

      sample = (app, id, predicted, actual, bytes)
      if app not in results:
        results[app] = []
      results[app].append(sample)

      if id == 0:
        beam_search[app] = sample

  print("Beam Search")
  for app in apps:
    if app not in beam_search:
      print("?")
      continue

    print(beam_search[app][ACTUAL])

  print("\nBest of 1")
  for app in apps:
    if app not in results:
      print(app)
      continue
    samples = results[app]
    bestof1 = bestofN(app, samples, 1)
    print(bestof1[0][ACTUAL])

  print("\nBest of 5")
  for app in apps:
    if app not in results:
      print(app)
      continue
    samples = results[app]
    bestof5 = bestofN(app, samples, 5)
    print(bestof5[0][ACTUAL])

  print("\nBest of 10")
  for app in apps:
    if app not in results:
      print(app)
      continue
    samples = results[app]
    bestof10 = bestofN(app, samples, 10)
    print(bestof10[0][ACTUAL])

  print("\nBest of 20")
  for app in apps:
    if app not in results:
      print(app)
      continue
    samples = results[app]
    bestof20 = bestofN(app, samples, 20)
    print(bestof20[0][ACTUAL])

  print("\nBest of 40")
  for app in apps:
    if app not in results:
      print(app)
      continue
    samples = results[app]
    bestof40 = bestofN(app, samples, 40)
    print(bestof40[0][ACTUAL])

  print("\nBest of 80")
  for app in apps:
    if app not in results:
      print(app)
      continue
    samples = results[app]
    bestof80 = bestofN(app, samples, 80)
    print(bestof80[0][ACTUAL])

if __name__ == "__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument("--filename", type=str, required=True)
  parser.add_argument("--app", type=str, required=True)

  args = parser.parse_args()
  predictions(args.app, args.filename)

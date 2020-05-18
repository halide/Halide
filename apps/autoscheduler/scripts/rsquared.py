from scipy import stats
import argparse
import pandas as pd
import pdb
import numpy as np

def mse(data, predicted_label, actual_label):
  return ((data[actual_label] - data[predicted_label]) ** 2).mean()

def relative_loss(data, predicted_label, actual_label):
  reference = np.min(data[actual_label])
  scale = 1.0 / reference

  actual = data[actual_label] * scale
  predicted = data[predicted_label] * scale
  predicted[predicted < 1e-10] = 1e-10

  return np.sum((1.0 / predicted - 1.0 / actual) ** 2)

def rsquared(data, predicted_label, actual_label):
  slope, intercept, r_value, p_value, std_err = stats.linregress(data[predicted_label], data[actual_label])
  return r_value ** 2

if __name__ == "__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument("--predictions", type=str, required=True)

  args = parser.parse_args()

  predicted_label = "Predicted"
  actual_label = "Actual"
  data = pd.read_csv(args.predictions, names=[predicted_label, actual_label])

  print("r-squared = {:.2f}".format(rsquared(data, predicted_label, actual_label)))
  print("loss = {:.2f}".format(relative_loss(data, predicted_label, actual_label)))


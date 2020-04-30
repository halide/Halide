from scipy import stats
import argparse
import pandas as pd
import pdb

def r_squared(predictions_file):
  predicted_label = "Predicted"
  actual_label = "Actual"
  data = pd.read_csv(predictions_file, names=[predicted_label, actual_label])

  slope, intercept, r_value, p_value, std_err = stats.linregress(data[predicted_label], data[actual_label])

  r_squared = r_value ** 2

  print("{:.2f}".format(r_squared))

if __name__ == "__main__":
  parser = argparse.ArgumentParser()
  parser.add_argument("--predictions", type=str, required=True)

  args = parser.parse_args()
  r_squared(args.predictions)

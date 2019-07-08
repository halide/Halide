"""Verifies the Halide operator functions properly."""

import torch as th
import halide_ops as ops

if __name__ == "__main__":
  a = th.ones(1, 2, 8, 8)
  b = th.ones(1, 2, 8, 8)*3
  output = th.zeros(1, 2, 8, 8)
  gt = th.ones(1, 2, 8, 8)*4

  ops.add(a, b, output)

  diff = (output-gt).sum().item()
  assert diff == 0.0, "Test failed: sum should be 4"
  print("Test ran successfully: difference is", diff)

  # TODO: CPU, GPU, int, float, double, others? size of buffer?

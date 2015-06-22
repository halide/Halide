#!/usr/bin/python3

import os
import sys

path0 = os.path.abspath(__file__)
#print(path0)

sys.path.append(os.path.join(path0, "../build"))
try:
    import halide         # First try to use the Python site-packages halide
except ImportError:
    sys.path = path0
    import halide         # If that fails use the local halide

print(halide.greet())
#halide.test()

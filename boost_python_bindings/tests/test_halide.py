import sys
path0 = sys.path
sys.path = sys.path[1:]
try:
    import halide         # First try to use the Python site-packages halide
except ImportError:
    sys.path = path0
    import halide         # If that fails use the local halide

halide.test()

HelloPytorch is a simple application that demonstrates how to build a
PyTorch wrapper around Halide ops using the internal Generator target.

The demo op takes two 4D tensors as input, `a` and `b` and outputs their
pointwise sum.

The application builds float32 and float64 version of the op for CPU and CUDA (is a device and `nvcc` compiler is
detected, see `Makefile`).

The build proceeds in four steps:
1. Build the Halide generator
2. Build Halide code for each each datatype and each arhitecture (`-e static_library,h` flag in the generator). 
3. Build the PyTorch C++ interface code (`-e pytorch_wrapper` flag in the
   generator). Note that the PyTorch wrapper requires the `user_context`
   feature in the generator `target` for CUDA ops. This allows Halide's and
   PyTorch's GPU memory managers to communicate with each other.
4. Synthesize and compile a Python extension that links togethers the various
   operator libraries and exposes them to Python (see `setup.py`).


The build requires Python 3 and PyTorch. Please follow the instructions here to
install PyTorch:
    https://pytorch.org/

If everything is setup correctly, running `make test` should build the PyTorch extension and runs a simple test (`test.py`).

This app provides benchmarks and tests for a number of common deep
learning network operations:

- AveragePool
- Convolution
- DepthwiseConvolution
- Im2col
- MatrixMultiply
- MaxPool

The benchmarks are set up to measure the performance of these
operations as used in an open-sourced MobileNet v1 model.

This app is intended to provide fast implementations of common
deep learning network operations on all platforms that Halide supports.

These operations were developed at Google and contributed by
David Chen, Ronald Wotzlaw, Huizhong Chen, and Volodymyr Kysenko (@vksnk
on GitHub).

TODOs and notes
===============

* Support data types other than 8-bit integers

* Optimize Hexagon schedules (some room for improvement)

* Optimize CPU schedules (much room for improvement)

* Implement GPU schedules

* Im2Col is used with MatrixMultiply to implement Convolution. This
approach makes sense when one has access to a good matrix multiply
implementation to use and cannot be modified easily. However, a good
schedule for Convolution should be at least as good as
Im2Col + MatrixMultiply (at worst, the two stages can be in the same
Halide pipeline, with Im2Col compute_root). Therefore, it might make
sense to spend effort optimizing Convolution rather than Im2Col.


Build and test
==============

To build and run these benchmarks and tests:

    HL_TARGET=host make run-host

To build and run these benchmarks and tests on an ARM Android device:

    HL_TARGET=arm-64-android ./adb_run_on_device.sh


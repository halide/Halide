# API Reference

Halide's front end is embedded in C++. You build a pipeline by writing C++ code
using {cpp:class}`Halide::Var`, {cpp:class}`Halide::Expr`, and
{cpp:class}`Halide::Func` objects, then either call
{cpp:func}`Halide::Func::compile_to_file` to generate an object file and header
(for deploying large routines ahead-of-time), or call
{cpp:func}`Halide::Func::realize` to JIT-compile and run the pipeline
immediately (good for testing small routines).

To learn Halide, start with the [tutorial](../tutorial/index). The `test/`
directory has many small examples exercising individual features, and the
`apps/` directory has larger, realistic pipelines -- `local_laplacian`,
`bilateral_grid`, and `interpolate` are good ones to start with.

If you're looking for a binary release, install a
[stable release](https://pypi.org/project/halide) or a
[nightly build](https://pypi.halide-lang.org/simple/halide) with pip or uv.
Installing `halide` includes the compiler bindings, native toolchain, and
standalone runtime bindings. Runtime-only deployments can install
`halide-runtime`. If you're building with CMake, see
[the Halide CMake helpers](../HalideCMakePackage).

## Where to start

For defining, scheduling, and evaluating basic pipelines:
{cpp:class}`Halide::Func`, {cpp:class}`Halide::Stage`, {cpp:class}`Halide::Var`

The image data type: {cpp:class}`Halide::Buffer`

For passing around and reusing Halide expressions: {cpp:class}`Halide::Expr`

For representing scalar and image parameters to pipelines:
{cpp:class}`Halide::Param`, {cpp:class}`Halide::ImageParam`

For writing functions that reduce or scatter over some domain:
{cpp:class}`Halide::RDom`

For writing and evaluating functions that return multiple values:
{cpp:class}`Halide::Tuple`, {cpp:class}`Halide::Realization`

```{toctree}
:titlesonly:
:maxdepth: 1

generated/reference
```

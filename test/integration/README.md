# Integration tests

These tests validate our CMake-built packages and make sure reasonable
interactions with the Halide-generated libraries and targets work. They run on
GitHub Actions, rather than the buildbots, to test building, installing, and
using Halide in **simple** cases from a clean build environment. In particular,
this folder **should not** be added to the main Halide build
via `add_subdirectory`.

The assumption is that we are building Halide with the latest CMake version, but
that our users might be on our oldest supported version. GitHub Actions makes it
easy to use two different versions on two different VMs.

There are scenarios here for JIT compilation, AOT compilation, and AOT _cross_
compilation from x86 to aarch64 (tested via Qemu). This test in particular cannot
be easily run on the buildbots because it requires two VMs: an Ubuntu build machine
for Halide, and an Ubuntu developer machine which installs the DEB packages.

Consult the `packaging.yml` workflow file for precise steps to run these locally.

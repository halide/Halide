# Dependencies

This folder contains vendored dependencies for building Halide. They do not
form part of the API surface.

## SPIR-V

This folder contains a copy of the officially released v1.6 ANSI-C header
file for [SPIR-V], obtained from the `sdk-1.3.231` branch
of https://github.com/KhronosGroup/SPIRV-Headers.

The directory structure within this folder matches that of the official
version's install tree, plus the upstream `LICENSE` notice, minus files
that Halide doesn't need.

The `update-spirv.sh` script will automatically acquire the upstream repo,
build it, and extract the necessary files. It takes a single argument, the
name of the branch to clone.

[SPIR-V]: https://www.khronos.org/registry/spir-v

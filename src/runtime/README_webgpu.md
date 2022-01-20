# WebGPU support for Halide

Halide has work-in-progress support for generating and running WebGPU shaders.
This can be used in conjunction with the WebAssembly backend to bring
GPU-accelerated Halide pipelines to the web.

As the first version of the WebGPU standard is itself still being developed,
Halide's support has some limitations and may only work with certain browsers
and versions of Emscripten.

## Known limitations

The following is a non-comprehensive list of known limitations:

-   Only 32-bit integers and floats are supported.
    * 8-bit and 16-bit integers will be implemented using emulation inside
      Halide and with future extensions to WGSL.
    * 64-bit integers and floats will likely remain unsupported.
-   Schedules that use workgroup memory are not yet supported.
-   Dynamic gpu_tile sizes are not yet supported.
-   Host<->Device copies are only implemented for contiguous data regions.
-   Device<->Device copies are not yet implemented.
-   Device buffer cropping/slicing is not yet implemented.
-   Wrapping native device buffer handles is not yet implemented.
-   Many intrinsic functions are not yet implemented.

In addition to these functional limitations, the performance of the WebGPU
backend has not yet been evaluated, and so optimizations in the runtime or
device codegen may be required before it becomes profitable to use.

## Running with WebAssembly via Emscripten: `HL_TARGET=wasm-32-wasmrt-webgpu`

> _Tested with top-of-tree Emscripten as of 2022-01-06, against Chrome v99._

Halide can generate WebGPU code that can be integrated with WASM code using
Emscripten.
Halide must currently be built *without* the `WEBGPU_NATIVE_LIB` flag when
targeting Emscripten.

When invoking `emcc` to link Halide-generated objects, include these flags:
`-s USE_WEBGPU=1 -s ASYNCIFY`.

Tests that use AOT compilation can be run using a native WebGPU implementation
that has Node.js bindings, such as [Dawn](dawn.googlesource.com/dawn/).
When configuring Halide, use `-DWEBGPU_NODE_BINDINGS=/path/to/dawn.node` to
enable these tests.
See [below](#setting-up-dawn) for instructions on building the Dawn Node.js
bindings.

JIT compilation is not supported when using WebGPU with WASM.

## Running natively: `HL_TARGET=host-webgpu`

For testing purposes, Halide can also target native WebGPU libraries, such as
[Dawn](dawn.googlesource.com/dawn/) or [wgpu](github.com/gfx-rs/wgpu).
This is currently the only path that can run the JIT correctness tests.
See [below](#setting-up-dawn) for instructions on building Dawn.

Due to differences between the APIs implemented by native WebGPU libraries and
Emscripten, this currently requires a separate build of Halide.
Pass `-DWEBGPU_NATIVE_LIB=/path/to/native/library.{so,dylib.dll}` to CMake when
configuring Halide to enable this path, which will automatically use this
library for the AOT and JIT tests.

## Setting up Dawn

Building Dawn's Node.js bindings currently requires using CMake.

First, [install `depot_tools`](commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up) and add it to the
`PATH` environment variable.

Next, get Dawn and its dependencies:

    # Clone the repo
    git clone https://dawn.googlesource.com/dawn
    cd dawn

    # Bootstrap the gclient configuration with Node.js bindings enabled
    cp scripts/standalone-with-node.gclient .gclient

    # Fetch external dependencies and toolchains with gclient
    gclient sync

Finally, build Dawn, enabling both the Node.js bindings and shared libraries:

    mkdir -p <build_dir>
    cd <build_dir>

    cmake <dawn_root_dir> -G Ninja
        -DCMAKE_BUILD_TYPE=Release \
        -DDAWN_BUILD_NODE_BINDINGS=1 \
        -DDAWN_ENABLE_PIC=1 \
        -DBUILD_SHARED_LIBS=ON

    ninja dawn.node webgpu_dawn

This will produce the following artifacts:
- Node.js bindings: `<build_dir>/dawn.node`
- Native library: `<build_dir>/src/dawn_native/libwebgpu_dawn.{so,dylib,dll}`

These paths can then be used for the `-DWEBGPU_NODE_BINDINGS` and
`-DWEBGPU_NATIVE_LIB` CMake options when configuring Halide.

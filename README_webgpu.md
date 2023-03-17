# WebGPU support for Halide

Halide has work-in-progress support for generating and running WebGPU shaders.
This can be used in conjunction with the WebAssembly backend to bring
GPU-accelerated Halide pipelines to the web.

As the first version of the WebGPU standard is itself still being developed,
Halide's support has some limitations and may only work with certain browsers
and versions of Emscripten.

## Known limitations

The following is a non-comprehensive list of known limitations:

-   Only 32-bit integers and floats have efficient support.
    * 8-bit and 16-bit integers are implemented using emulation. Future
      extensions to WGSL will allow them to be implemented more efficiently.
    * 64-bit integers and floats will likely remain unsupported until WGSL gains
      extensions to support them.
-   Wrapping native device buffer handles is not yet implemented.

In addition to these functional limitations, the performance of the WebGPU
backend has not yet been evaluated, and so optimizations in the runtime or
device codegen may be required before it becomes profitable to use.

## Running with WebAssembly via Emscripten: `HL_TARGET=wasm-32-wasmrt-webgpu`

> _Tested with top-of-tree Emscripten as of 2023-02-23, against Chrome v113._

Halide can generate WebGPU code that can be integrated with WASM code using
Emscripten.

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

> _Tested with top-of-tree Dawn as of 2023-03-14._

For testing purposes, Halide can also target native WebGPU libraries, such as
[Dawn](dawn.googlesource.com/dawn/) or [wgpu](github.com/gfx-rs/wgpu).
This is currently the only path that can run the JIT correctness tests.
See [below](#setting-up-dawn) for instructions on building Dawn.

Pass `-DWEBGPU_NATIVE_LIB=/path/to/native/library.{so,dylib.dll}` to CMake when
configuring Halide to enable this path, which will automatically use this
library for the AOT and JIT tests.

Note that it is explicitly legal to specify both WEBGPU_NATIVE_LIB and
WEBGPU_NODE_BINDINGS for the same build; the correct executable environment
will be selected based on the Halide target specified.

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

    # Other dependencies that must be installed manually:
    # - golang

Finally, build Dawn, enabling both the Node.js bindings and shared libraries:

    mkdir -p <build_dir>
    cd <build_dir>

    cmake <dawn_root_dir> -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DDAWN_BUILD_NODE_BINDINGS=1 \
        -DDAWN_ENABLE_PIC=1 \
        -DBUILD_SHARED_LIBS=ON

    ninja dawn.node webgpu_dawn

This will produce the following artifacts:
- Node.js bindings: `<build_dir>/dawn.node`
- Native library: `<build_dir>/src/dawn/native/libwebgpu_dawn.{so,dylib,dll}`

These paths can then be used for the `-DWEBGPU_NODE_BINDINGS` and
`-DWEBGPU_NATIVE_LIB` CMake options when configuring Halide.

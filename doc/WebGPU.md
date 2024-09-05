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
-   You must use CMake/CTest to build/test Halide for WebGPU; using the Makefile
    is not supported for WebGPU testing (and probably never will be).

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
that has Node.js bindings, such as [Dawn](https://dawn.googlesource.com/dawn/).
You must set an environment variable named `HL_WEBGPU_NODE_BINDINGS` that
has an absolute path to the bindings to run these tests, e.g. `HL_WEBGPU_NODE_BINDINGS=/path/to/dawn.node`.

See [below](#setting-up-dawn) for instructions on building the Dawn Node.js
bindings.

JIT compilation is not supported when using WebGPU with WASM.

## Running natively: `HL_TARGET=host-webgpu`

> _Tested with top-of-tree Dawn as of 2023-11-27 [commit b5d38fc7dc2a20081312c95e379c4a918df8b7d4]._

For testing purposes, Halide can also target native WebGPU libraries, such as
[Dawn](https://dawn.googlesource.com/dawn/) or
[wgpu](https://github.com/gfx-rs/wgpu).
This is currently the only path that can run the JIT correctness tests.
See [below](#setting-up-dawn) for instructions on building Dawn.

> Note that as of 2023-11-27, wgpu is not supported due to
> [lacking `override` support for WGSL](https://github.com/gfx-rs/wgpu/issues/1762)
> which we require > in order to set GPU block sizes.

When targeting WebGPU with a native target, Halide defaults to looking for a
build of Dawn (with several common names and suffixes); you can override this
by setting the `HL_WEBGPU_NATIVE_LIB` environment variable to the absolute path
to the library you want.

Note that it is explicitly legal to define both `HL_WEBGPU_NATIVE_LIB` and
`HL_WEBGPU_NODE_BINDINGS` at the same time; the correct executable environment
will be selected based on the Halide target specified.

Note that it is explicitly legal to specify both WEBGPU_NATIVE_LIB and
WEBGPU_NODE_BINDINGS for the same build; the correct executable environment
will be selected based on the Halide target specified.

## Setting up Dawn

Building Dawn's Node.js bindings currently requires using CMake.

First, [install `depot_tools`](https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up) and add it to the
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

These paths can then be used for the `HL_WEBGPU_NODE_BINDINGS` and
`HL_WEBGPU_NATIVE_LIB` environment variables when using Halide.

## Updating mini_webgpu.h

The recommended method for updating `mini_webgpu.h` is to copy the
`gen/include/dawn/webgpu.h` file from the Dawn build directory, then:
- Restore the `// clang-format {off,on}` lines.
- Comment out the `#include <std*>` lines.
- Remove the `void` parameter from the `WGPUProc` declaration.

This guarantees a version of the WebGPU header that is compatible with Dawn.
When the native API eventually stabilizes, it should be possible to obtain a
header from the `webgpu-native` GitHub organization that will be compatible
with Dawn, wgpu, and Emscripten.

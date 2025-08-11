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
You must have the Dawn Node.js bindings installed to run these tests. Set 
the `NODE_PATH` environment variable to the path to the `node_modules`
directory of the Dawn bindings.

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

> Note that as of 2023-11-27, wgpu is not supported due to [lacking `override`
> support for WGSL](https://github.com/gfx-rs/wgpu/issues/1762) which we require
> to set GPU block sizes.

When targeting WebGPU with a native target, Halide defaults to looking for a
build of Dawn (with several common names and suffixes); you can override this
by setting the `HL_WEBGPU_NATIVE_LIB` environment variable to the absolute path
to the library you want.

## Setting up Dawn

Building Dawn's Node.js bindings currently requires using CMake. Start by
cloning Dawn and its dependencies:

    # Clone the repo
    git clone https://dawn.googlesource.com/dawn
    cd dawn

    # Fetch external dependencies
    python3 tools/fetch_dawn_dependencies.py

    # Their script misses a few:
    git submodule update --init --depth 1 third_party/gpuweb
    git submodule update --init --depth 1 third_party/node-addon-api
    git submodule update --init --depth 1 third_party/node-api-headers

    # Other dependencies that must be installed manually:
    # - golang

Now patch Dawn to install the Node.js bindings:

    echo "install(TARGETS dawn_node LIBRARY DESTINATION lib/node_modules)" >> src/dawn/node/CMakeLists.txt

Finally, build Dawn, enabling both the Node.js bindings and shared libraries:

    cmake -G Ninja -S . -B out/Release \
      -DCMAKE_BUILD_TYPE=Release \
      -DDAWN_BUILD_MONOLITHIC_LIBRARY=SHARED \
      -DDAWN_BUILD_NODE_BINDINGS=ON \
      -DDAWN_BUILD_PROTOBUF=OFF \
      -DDAWN_BUILD_SAMPLES=OFF \
      -DDAWN_BUILD_TESTS=OFF \
      -DDAWN_ENABLE_INSTALL=ON \
      -DDAWN_ENABLE_PIC=ON \
      -DTINT_BUILD_CMD_TOOLS=OFF \
      -DTINT_BUILD_TESTS=OFF
    cmake --build out/Release
    cmake --install out/Release --prefix /opt/dawn

This will produce the following artifacts:
- Node.js bindings: `/opt/dawn/lib/node_modules/dawn.node`
- Native library: `/opt/dawn/lib/libwebgpu_dawn.{so,dylib,dll}`

You can then add `/opt/dawn/lib/node_modules` to `NODE_PATH` and `/opt/dawn/lib`
to `LD_LIBRARY_PATH`. You are, of course, free to choose any other installation
prefix besides `/opt/dawn` (e.g. `/usr/local`).

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

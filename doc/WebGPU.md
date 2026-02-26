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

> _Tested with Emscripten 5.0.0 (a7c5deabd7c88ba1c38ebe988112256775f944c6)_
> _Tested with Node.js 25.5.0_

Halide can generate WebGPU code that can be integrated with WASM code using
Emscripten.

When invoking `emcc` to link Halide-generated objects, include these flags:
`--use-port=emdawnwebgpu -s JSPI`.

Tests that use AOT compilation can be run using a WebGPU implementation
that has Node.js bindings, such as [Dawn](https://dawn.googlesource.com/dawn/).
You must set an environment variable named `HL_WEBGPU_NODE_BINDINGS` that
has an absolute path to the bindings to run these tests, e.g. `HL_WEBGPU_NODE_BINDINGS=/path/to/dawn.node`.

See [below](#setting-up-dawn) for instructions on building the Dawn Node.js
bindings.

JIT compilation is not supported when using WebGPU with WASM.

## Running natively: `HL_TARGET=host-webgpu`

> _Tested with Dawn release branch chromium/7698 (536c572aba)_

For testing purposes, Halide can also target native WebGPU libraries, such as
[Dawn](https://dawn.googlesource.com/dawn/) or
[wgpu](https://github.com/gfx-rs/wgpu).
This is currently the only path that can run the JIT correctness tests.
See [below](#setting-up-dawn) for instructions on building Dawn.

> Note that as of 2026-02-17, wgpu is not supported due to lack of WaitAny timeout support.

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

## Setting up Dawn via vcpkg

> _Tested with Dawn vcpkg port version 20251202.213730 on macOS (arm64)_

The Halide repository includes vcpkg support for automatically downloading and
building Dawn as part of the CMake configure step. This is the easiest path for
native WebGPU testing and does not require `depot_tools` or `gclient`.

Note: the vcpkg Dawn port builds only the native `libwebgpu_dawn` shared
library. It does **not** include Node.js bindings (`dawn.node`), so this path
only supports `HL_TARGET=host-webgpu` (native JIT). For the WASM+Node.js path,
see [Setting up Dawn](#setting-up-dawn) below.

### Install vcpkg

Clone and bootstrap vcpkg locally (no system-wide installation needed):

    git clone https://github.com/microsoft/vcpkg <vcpkg_dir>
    <vcpkg_dir>/bootstrap-vcpkg.sh -disableMetrics

### Configure and build Halide

Use the provided `arm64-osx-halide` (or `x64-osx-halide`) overlay triplet,
which keeps static linkage for all packages except Dawn (which must be shared
so it can be loaded via `dlopen` at runtime):

    cmake <halide_root_dir> -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=<vcpkg_dir>/scripts/buildsystems/vcpkg.cmake \
        -DVCPKG_TARGET_TRIPLET=arm64-osx-halide \
        -DVCPKG_MANIFEST_FEATURES=webgpu-native \
        -DLLVM_DIR=<llvm_dir>/lib/cmake/llvm \
        -DClang_DIR=<llvm_dir>/lib/cmake/clang \
        -DHalide_TARGET=host-webgpu

    cmake --build <build_dir>

vcpkg will download and build Dawn automatically during the configure step,
installing it into `<build_dir>/vcpkg_installed/`.

You may need to add `-DWITH_SERIALIZATION=OFF`, `-DWITH_PYTHON_BINDINGS=OFF`, and
`-DHalide_WASM_BACKEND=OFF` flags because vcpkg disables FetchContent, and those 
features depend on packages (`flatbuffers`, `pybind11`, `wabt`) that are not included 
in the `webgpu-native` manifest feature.

### Run tests

Set `HL_WEBGPU_NATIVE_LIB` to the vcpkg-installed Dawn library and run via
CTest:

    HL_JIT_TARGET=host-webgpu \
    HL_WEBGPU_NATIVE_LIB=<build_dir>/vcpkg_installed/arm64-osx-halide/lib/libwebgpu_dawn.dylib \
    ctest --test-dir <build_dir> -R correctness_gpu

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
- Include the following block to define things that would normally be defined in system headers:
```
// BEGIN Halide-specific changes
//
// For the Halide runtime, we can't include these headers,
// so we define NULL, SIZE_MAX, and integer limit macros here.
// #include <stdint.h>
// #include <stddef.h>
// #include <math.h>

#ifndef NULL
#ifdef __cplusplus
#define NULL nullptr
#else
#define NULL ((void*)0)
#endif
#endif

#ifndef SIZE_MAX
#define SIZE_MAX (~(size_t)0)
#endif

#ifndef UINT32_MAX
#define UINT32_MAX (~(uint32_t)0)
#endif

#ifndef UINT64_MAX
#define UINT64_MAX (~(uint64_t)0)
#endif

// This _should_ be correct on all platforms we support, but needs checking.
#ifndef UINT32_C
#define UINT32_C(x) ((uint32_t)(x))
#endif

// END Halide-specific changes

```

This guarantees a version of the WebGPU header that is compatible with how
Halide builds the runtime.

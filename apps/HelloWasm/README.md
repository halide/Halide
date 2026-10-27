# HelloWasm

A demonstration of integrating a Halide pipeline into a WebAssembly application:
a reaction-diffusion simulation, AOT-compiled by Halide to four WASM variants
(scalar/SIMD x single/multi-threaded), linked into a browser demo with
Emscripten, and switched between at runtime based on what the browser actually
supports.

## How the pieces fit together

- `generator/reaction_diffusion_generator.cpp` defines the pipeline (three
  Halide generators: `init`, `update`, `render`). Generators always run on the
  *host*, regardless of what target they emit code for -- this is true even when
  that target is `wasm-32-wasmrt`.
- `app/core.cpp` is the actual WASM application: an SDL2 program (compiled by
  Emscripten) that calls into the AOT-compiled pipeline each frame and blits the
  result to a `<canvas>`.
- Because the generator needs a host compiler and `core.cpp` needs the
  Emscripten compiler, this is a CMake ["super-build"][super-build]: the
  top-level `CMakeLists.txt` builds `generator/` with the host compiler first,
  then configures `app/` under Emscripten's own CMake toolchain file, importing
  the host-built generator to emit the pipeline for four `wasm-32-wasmrt`
  variants (`FEATURES wasm_simd128`/`wasm_threads`, or neither).
- `index.html` picks one of the four compiled variants at load time based on
  `?simd`/`?threads` URL parameters, falling back to the single-threaded scalar
  build by default.

## Building

Prerequisites: a Halide install (built via CMake -- see
`doc/BuildingHalideWithCMake.md`) and the [Emscripten SDK][emsdk]. The build
finds `emcc` via `$EMSDK` if you've sourced `emsdk_env.sh`, or otherwise via
`PATH`.

```
cmake -S apps/HelloWasm -B build -DCMAKE_PREFIX_PATH=<path/to/halide install>
cmake --build build
ctest --test-dir build --output-on-failure
```

All build output (the four `index*.js`/`.wasm` pairs, plus copies of
`index.html`/`main.js`/`sw.js`) lands under `build/app/` -- nothing is ever
written back into the source tree.

## Running the demo

```
cd build/app
python3 -m http.server 8080
```

Then visit <http://127.0.0.1:8080/index.html>. The single-threaded scalar
variant should work in any modern browser; the SIMD and multi-threaded variants
may require enabling experimental WebAssembly features (e.g. in Chrome,
`chrome://flags`).

[emsdk]: https://emscripten.org/docs/getting_started/downloads.html
[super-build]: ../../doc/HalideCMakePackage.md#use-a-super-build

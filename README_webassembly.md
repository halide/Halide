# WebAssembly Support for Halide

Halide supports WebAssembly (Wasm) code generation from Halide using the LLVM backend.

As WebAssembly itself is still under active development, Halide's support has some limitations. Some of the most important:

- We require using LLVM 9+ for Wasm codegen; earlier versions of LLVM may work, but have not been tested, and so are currently deliberately excluded.
- SIMD can be enabled via Target::WasmSimd128, but is likely to require building with LLVM 9+ and running in V8 7.5 or later to work properly. (Earlier versions of V8 should work for non-SIMD usage.)
- Multithreading is not yet ready to be supported -- there isn't even a Feature flag to enable it yet -- but we would like to add support for this in the future.
- Halide's JIT for Wasm is extremely limited and really useful only for internal testing purposes.

# Additional Tooling Requirements:
- In additional to the usual install of LLVM and Clang, you'll need wasm-ld (via LLVM/tools/lld). All should be v9.x+ (current trunk as of April 2019).
- V8 library, v7.0+
- d8 shell tool, v7.0+
- Emscripten, 1.38.28+

Note that for all of the above, earlier versions might work, but have not been tested.

# AOT Limitations

Halide outputs a Wasm object (.o) or static library (.a) file, much like any other architecture; to use it, of course, you must link it to suitable calling code. Additionally, you must link to something that provides an implementation of `libc`; as a practical matter, this means using the Emscripten tool to do your linking, as it provides the most complete such implementation we're aware of at this time. (It is hoped that WASI [https://wasi.dev/] will provide a good alternative solution at some point.)

- Halide ahead-of-time tests assume/require that you have Emscripten installed and available on your system.

- Halide doesn't support multithreading in Wasm just yet; we hope to add that in the future.

# JIT Limitations

It's important to reiterate that the WebAssembly JIT mode is not (and will never be) appropriate for anything other than limited self tests, for a number of reasons:

- It requires linking both an instance of the V8 library and LLVM's wasm-ld tool into libHalide. (We would like to offer support for other Wasm engines in the future, e.g. SpiderMonkey, to provide more balanced testing, but there is no timetable for this.)
- Every JIT invocation requires redundant recompilation of the Halide runtime. (This could be improved when the LLVM Wasm backend has better support for `dlopen()`.)
- Wasm effectively runs in a private, 32-bit memory address space; while the host has access to that entire space, the reverse is not true, and thus any `define_extern` calls require copying all `halide_buffer_t` data across the Wasm<->host boundary in both directions. This has severe implications for existing benchmarks, which don't currently attempt to account for this extra overhead. (This could possibly be improved by modeling the Wasm JIT's buffer support as a `device` model that would allow lazy copy-on-demand.)
- Host functions used via `define_extern` or `HalideExtern` cannot accept or return values that are pointer types or 64-bit integer types; this includes things like `const char *` and `user_context`. Fixing this is tractable, but is currently omitted as the fix is nontrivial and the tests that are affected are mostly non-critical. (Note that `halide_buffer_t*` is explicitly supported as a special case, however.)
- Threading isn't supported at all (yet); all `parallel()` schedules will be run serially.
- The `.async()` directive isn't supported at all, not even in serial-emulation mode.
- You can't use `Param<void *>` (or any other arbitrary pointer type) with the Wasm jit.
- You can't use `Func.debug_to_file()`, `Func.set_custom_do_par_for()`, `Func.set_custom_do_task()`, or `Func.set_custom_allocator()`.
- The implementation of `malloc()` used by the JIT is incredibly simpleminded and unsuitable for anything other than the most basic of tests.
- GPU usage (or any buffer usage that isn't 100% host-memory) isn't supported at all yet. (This should be doable, just omitted for now.)

Note that while some of these limitations may be improved in the future, some are effectively intrinsic to the nature of this problem. Realistically, this JIT implementation is intended solely for running Halide self-tests (and even then, a number of them are fundamentally impractical to support in a hosted-Wasm environment and are blacklisted).

In sum: don't plan on using Halide JIT mode with Wasm unless you are working on the Halide library itself.

# To Use Halide For WebAssembly:

- Ensure WebAssembly is in LLVM_TARGETS_TO_BUILD; if you use the default (`"all"`) then it's already present, but otherwise, add it explicitly:
```
-DLLVM_TARGETS_TO_BUILD="X86;ARM;NVPTX;AArch64;Mips;PowerPC;Hexagon;WebAssembly
```

## Enabling wasm JIT
If you want to run `test_correctness` and other interesting parts of the Halide test suite (and you almost certainly will), you'll need to install libV8 and ensure that LLVM is built with wasm-ld:

- Ensure that you have tools/lld in your LLVM build checkout:
```
svn co https://llvm.org/svn/llvm-project/lld/trunk /path/to/llvm-trunk/tools/lld
```

(You might have to do a clean build of LLVM for CMake to notice that you've added a tool.)

- Install libv8 and the d8 shell tool (instructions omitted), or build from source if you prefer (instructions omitted). We are able to compile with v7.0+ (but not earlier versions), but there are SIMD-related bugs in versions prior to v7.5 that will cause many of our tests to fail; if you are going to test with `wasm_simd128` at all, you should really use V8 v7.5 or later.

Note that using shared-library builds of V8 may be problematic on some platforms (e.g. OSX) due to libc++ conflict issues; using a static-library version may be simpler for those. Also note that (as of this writing), libV8 v7.5+ may not be available in prebuilt form for your platform.

- Set `V8_INCLUDE_PATH` and `V8_LIB_PATH` to point to the paths for V8 include files and library, respectively.

- Set `WITH_V8=1`

- To run the JIT tests, set `HL_JIT_TARGET=wasm-32-wasmrt` (or `HL_JIT_TARGET=wasm-32-wasmrt-wasm_simd128`) and run normally. The test suites which we have vetted to work include correctness, performance, error, and warning. (Some of the others could likely be made to work with modest effort.)

## Enabling wasm AOT

If you want to test ahead-of-time code generation (and you almost certainly will), you need to install Emscripten and a shell for running wasm+js code (e.g., d8, part of v8)

- The simplest way to install is probably via the Emscripten emsdk (https://emscripten.org/docs/getting_started/downloads.html).

- The default Halide makefile sets a custom value for `EM_CONFIG` to ensure that we use the correct version of LLVM (i.e., the version used by the rest of Halide), rather than relying on `~/.emscripten` being set correctly. If you are using Emscripten in your own build system in conjunction with Halide, you'll probably need to edit your own `~/.emscripten` file to ensure that `LLVM_ROOT` points at the LLVM you built earlier (or pass a custom `--em-config` flag, or set the `EM_CONFIG` env var). If you fail with errors like `WASM_BACKEND selected but could not find lld (wasm-ld)`, you forgot to do this step.

- Set `WASM_SHELL=/path/to/d8`

- To run the AOT tests, set `HL_TARGET=wasm-32-wasmrt` and build the `test_aotwasm_generator` target. (Note that the normal AOT tests won't run usefully with this target, as extra logic to run under a wasm-enabled shell is required, and some tests are blacklisted.)

# Running benchmarks

The `test_performance` benchmarks are misleading (and thus useless) for Wasm, as they include JIT overhead as described elsewhere. Instead, you should use `make benchmark_apps`, which will build and run a handful of targets in the `apps/` folder using the standard Halide benchmarking utility. (It is smart enough to special-case when HL_TARGET is set to any `wasm-32-wasmrt` variant.)

```
    # benchmark for whatever HL_TARGET is already set to (probably 'host')
    $ make benchmark_apps

    # benchmark for baseline wasm
    $ HL_TARGET=wasm-32-wasmrt make benchmark_apps

    # benchmark for wasm with SIMD128 (note that some benchmarks will crash when
    # running with V8 7.5x builds, due to apparently-known bugs)
    $ HL_TARGET=wasm-32-wasmrt-wasm_simd128 make benchmark_apps
```

Also note that if you run the above on a typical desktop system, you'll find the `host` benchmarks 10x faster (or more) the wasm; this is largely because your desktop likely has multiple cores (and is making use of them), while our Wasm generation doesn't yet support threading. For a fairer comparison, you can limit the maximum number of threads used by Halide by setting the `HL_NUM_THREADS` env var, e.g.

```
    # benchmark for whatever HL_TARGET is already set to (probably 'host'),
    # ensuring that we never use more than one thread at a time (regardless of
    # the number of CPU cores on the host).
    $ HL_NUM_THREADS=1 make benchmark_apps
```

# Known Limitations And Caveats
- We have only tested with EMCC_WASM_BACKEND=1; using the fastcomp backend could possibly be made to work, but we haven't attempted to do so and aren't planning on doing so in the forseeable future. (Patches to enable this would be considered.)
- Using the JIT requires that we link the `wasm-ld` tool into libHalide; with some work this need could possibly be eliminated.
- CMake support hasn't been investigated yet, but should be straightforward. (Patches welcome.)
- OSX and Linux-x64 have been tested. Windows hasn't; it should be supportable with some work. (Patches welcome.)
- None of the `apps/` folder has been investigated yet. Many of them should be supportable with some work. (Patches welcome.)
- We currently use d8 as a test environment for AOT code; we should probably consider using Node or (better yet) headless Chrome instead (which is probably required to allow for using threads in AOT code).


# Known TODO:

- There's some invasive hackiness in Codgen_LLVM to support the JIT trampolines; this really should be refactored to be less hacky.
- Can we rework JIT to avoid the need to link in wasm-ld? This might be doable, as the wasm object files produced by the LLVM backend are close enough to an executable form that we could likely make it work with some massaging on our side, but it's not clear whether this would be a bad idea or not (i.e., would it be unreasonably fragile).
- Improve the JIT to allow more of the tests to run; in particular, externs with 64-bit arguments (doable but omitted for expediency) and GPU support (ditto).
- Buffer-copying overhead in the JIT could possibly be dramatically improved by modeling the copy as a "device" (i.e. `copy_to_device()` would copy from host -> wasm); this would make the performance benchmarks much more useful.
- Can we support threads in the JIT without an unreasonable amount of work? Unknown at this point.
- Someday, we should support alternate JIT/AOT test environments (e.g. SpiderMonkey/Firefox).



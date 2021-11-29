# WebAssembly Support for Halide

Halide supports WebAssembly (Wasm) code generation from Halide using the LLVM
backend.

As WebAssembly itself is still under active development, Halide's support has
some limitations. Some of the most important:

-   Fixed-width SIMD (128 bit) can be enabled via Target::WasmSimd128.
-   Sign-extension operations can be enabled via Target::WasmSignExt.
-   Non-trapping float-to-int conversions can be enabled via
    Target::WasmSatFloatToInt.
-   Threads have very limited support via Target::WasmThreads; see
    [below](#using-threads) for more details.
-   Halide's JIT for Wasm is extremely limited and really useful only for
    internal testing purposes.

# Additional Tooling Requirements:

-   In additional to the usual install of LLVM and clang, you'll need lld.
-   Locally-installed version of Emscripten, 1.39.19+

Note that for all of the above, earlier versions might work, but have not been
tested.

# AOT Limitations

Halide outputs a Wasm object (.o) or static library (.a) file, much like any
other architecture; to use it, of course, you must link it to suitable calling
code. Additionally, you must link to something that provides an implementation
of `libc`; as a practical matter, this means using the Emscripten tool to do
your linking, as it provides the most complete such implementation we're aware
of at this time.

-   Halide ahead-of-time tests assume/require that you have Emscripten installed
    and available on your system, with the `EMSDK` environment variable set
    properly.

# JIT Limitations

It's important to reiterate that the WebAssembly JIT mode is not (and will never
be) appropriate for anything other than limited self tests, for a number of
reasons:

-   It actually uses an interpreter (from the WABT toolkit
    [https://github.com/WebAssembly/wabt]) to execute wasm bytecode; not
    surprisingly, this can be *very* slow.
-   Wasm effectively runs in a private, 32-bit memory address space; while the
    host has access to that entire space, the reverse is not true, and thus any
    `define_extern` calls require copying all `halide_buffer_t` data across the
    Wasm<->host boundary in both directions. This has severe implications for
    existing benchmarks, which don't currently attempt to account for this extra
    overhead. (This could possibly be improved by modeling the Wasm JIT's buffer
    support as a `device` model that would allow lazy copy-on-demand.)
-   Host functions used via `define_extern` or `HalideExtern` cannot accept or
    return values that are pointer types or 64-bit integer types; this includes
    things like `const char *` and `user_context`. Fixing this is tractable, but
    is currently omitted as the fix is nontrivial and the tests that are
    affected are mostly non-critical. (Note that `halide_buffer_t*` is
    explicitly supported as a special case, however.)
-   Threading isn't supported at all (yet); all `parallel()` schedules will be
    run serially.
-   The `.async()` directive isn't supported at all, not even in
    serial-emulation mode.
-   You can't use `Param<void *>` (or any other arbitrary pointer type) with the
    Wasm jit.
-   You can't use `Func.debug_to_file()`, `Func.set_custom_do_par_for()`,
    `Func.set_custom_do_task()`, or `Func.set_custom_allocator()`.
-   The implementation of `malloc()` used by the JIT is incredibly simpleminded
    and unsuitable for anything other than the most basic of tests.
-   GPU usage (or any buffer usage that isn't 100% host-memory) isn't supported
    at all yet. (This should be doable, just omitted for now.)

Note that while some of these limitations may be improved in the future, some
are effectively intrinsic to the nature of this problem. Realistically, this JIT
implementation is intended solely for running Halide self-tests (and even then,
a number of them are fundamentally impractical to support in a hosted-Wasm
environment and are disabled).

In sum: don't plan on using Halide JIT mode with Wasm unless you are working on
the Halide library itself.

# To Use Halide For WebAssembly:

-   Ensure WebAssembly is in LLVM_TARGETS_TO_BUILD; if you use the default
    (`"all"`) then it's already present, but otherwise, add it explicitly:

```
-DLLVM_TARGETS_TO_BUILD="X86;ARM;NVPTX;AArch64;Mips;PowerPC;Hexagon;WebAssembly
```

## Enabling wasm JIT

If you want to run `test_correctness` and other interesting parts of the Halide
test suite (and you almost certainly will), you'll need to ensure that LLVM is
built with wasm-ld:

-   Ensure that you have lld in LVM_ENABLE_PROJECTS:

```
cmake -DLLVM_ENABLE_PROJECTS="clang;lld" ...
```

-   To run the JIT tests, set `HL_JIT_TARGET=wasm-32-wasmrt` (possibly adding
    `wasm_simd128`, `wasm_signext`, and/or `wasm_sat_float_to_int`) and run
    CMake/CTest normally. Note that wasm testing is only support under CMake
    (not via Make).

## Enabling wasm AOT

If you want to test ahead-of-time code generation (and you almost certainly
will), you need to install Emscripten locally.

-   The simplest way to install is probably via the Emscripten emsdk
    (https://emscripten.org/docs/getting_started/downloads.html).

-   To run the AOT tests, set `HL_TARGET=wasm-32-wasmrt` (possibly adding
    `wasm_simd128`, `wasm_signext`, and/or `wasm_sat_float_to_int`) and run
    CMake/CTest normally. Note that wasm testing is only support under CMake
    (not via Make).

# Running benchmarks

The `test_performance` benchmarks are misleading (and thus useless) for Wasm, as
they include JIT overhead as described elsewhere. Suitable benchmarks for Wasm
will be provided at a later date. (See
https://github.com/halide/Halide/issues/5119 and
https://github.com/halide/Halide/issues/5047 to track progress.)

# Using Threads

You can use the `wasm_threads` feature to enable use of a normal pthread-based
thread pool in Halide code, but with some careful caveats:

-   This requires that you use a wasm runtime environment that provides
    pthread-compatible wrappers. At this time of this writing, the only
    environment known to support this well is Emscripten (when using the
    `-pthread` flag, and compiling for a Web environment). In this
    configuration, Emscripten goes to great lengths to make WebWorkers available
    via the pthreads API. (You can see an example of this usage in
    apps/HelloWasm.) Note that not all wasm runtimes support WebWorkers;
    generally, you need a full browser environment to make this work (though
    some versions of some shell tools may also support this, e.g. nodejs).
-   There is currently no support for using threads in a WASI environment, due
    to current limitations in the WASI specification. (We hope that this will
    improve in the future.)
-   There is no support for using threads in the Halide JIT environment, and no
    plans to add them anytime in the near-term future.

# Known Limitations And Caveats

-   Current trunk LLVM (as of July 2020) doesn't reliably generate all of the
    Wasm SIMD ops that are available; see
    https://github.com/halide/Halide/issues/5130 for tracking information as
    these are fixed.
-   Using the JIT requires that we link the `wasm-ld` tool into libHalide; with
    some work this need could possibly be eliminated.
-   OSX and Linux-x64 have been tested. Windows hasn't; it should be supportable
    with some work. (Patches welcome.)
-   None of the `apps/` folder has been investigated yet. Many of them should be
    supportable with some work. (Patches welcome.)
-   We currently use v8/d8 as a test environment for AOT code; we may want to
    consider using Node or (better yet) headless Chrome instead (which is
    probably required to allow for using threads in AOT code).

# Known TODO:

-   There's some invasive hackiness in Codgen_LLVM to support the JIT
    trampolines; this really should be refactored to be less hacky.
-   Can we rework JIT to avoid the need to link in wasm-ld? This might be
    doable, as the wasm object files produced by the LLVM backend are close
    enough to an executable form that we could likely make it work with some
    massaging on our side, but it's not clear whether this would be a bad idea
    or not (i.e., would it be unreasonably fragile).
-   Buffer-copying overhead in the JIT could possibly be dramatically improved
    by modeling the copy as a "device" (i.e. `copy_to_device()` would copy from
    host -> wasm); this would make the performance benchmarks much more useful.
-   Can we support threads in the JIT without an unreasonable amount of work?
    Unknown at this point.

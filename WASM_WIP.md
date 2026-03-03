# Halide WebAssembly Build — Work-in-Progress Notes

## Summary

We successfully built the Halide compiler as a WebAssembly module using
Emscripten. The compiler runs under Node.js and can perform AOT code generation
targeting x86-64, ARM, WebAssembly, and any other LLVM backend. JIT is not yet
supported (deferred to future work).

**Status: Smoke tests pass.** Three tests verified working:
1. Basic pipeline compilation → x86-64 bitcode
2. Scheduled pipeline (3×3 blur with tiling) → x86-64+SSE4.1 bitcode
3. Wasm-from-wasm: wasm-hosted compiler generating wasm target output

## Architecture

The build is a multi-stage cross-compilation:

```
Stage 1: Build LLVM as wasm32 static libraries (via emcmake)
         Inputs:  LLVM 20 source, native llvm-tblgen/clang-tblgen
         Outputs: ~158 .a files (444 MB total)

Stage 2: Cross-compile Halide against wasm LLVM (via emcmake)
         Inputs:  Halide source, wasm LLVM libs, native clang/llvm-as
         Outputs: libHalide.a (39 MB), gengen.wasm (44 MB)

Stage 3: Build and run test programs against libHalide.a (via em++)
         Outputs: test_halide_wasm.wasm (45 MB), runs under node
```

The key insight is that cross-compiling Halide requires **native host tools**
for two purposes:
- **LLVM tablegen**: `llvm-tblgen` and `clang-tblgen` must run on the host
  during the LLVM build to generate .inc files
- **Runtime bitcode compilation**: Halide compiles its runtime `.cpp` files to
  LLVM bitcode (`.ll` → `.bc`) at build time using `clang` and `llvm-as`. These
  must be native tools since they produce target-independent bitcode.

## Branch and Commits

Branch: `claude/halide-wasm-build-Qm4Lg`

- `aa4deb2` — Add wasm build support for running Halide compiler as WebAssembly
  - `wasm/build.sh` — Multi-stage build script
  - `wasm/test_halide_wasm.cpp` — Smoke test (3 tests)
  - `src/runtime/CMakeLists.txt` — Added `Halide_NATIVE_CLANG` / `Halide_NATIVE_LLVM_AS` cache variables

- `fe759aa` — Fix cross-compilation issues for building Halide as WebAssembly
  - `cmake/FindHalide_LLVM.cmake` — Find LLD before Clang for monorepo builds
  - `src/Target.cpp` — Emscripten guard in `calculate_host_target()`
  - `src/Util.cpp` — Disable makecontext/swapcontext + guard posix_spawnp
  - `tools/CMakeLists.txt` — NODERAWFS for build-time tools

## Files Modified (relative to main)

### `src/runtime/CMakeLists.txt`
Added two cache variables (`Halide_NATIVE_CLANG`, `Halide_NATIVE_LLVM_AS`)
that allow overriding the clang and llvm-as used for runtime bitcode
compilation. When cross-compiling, the LLVM-provided `clang` target points to
the wasm binary, which can't compile runtime .cpp files. The native overrides
bypass this.

The three `add_custom_command` blocks that compile runtime .cpp → .ll → .bc
were updated to use `${_halide_clang_cmd}` and `${_halide_llvm_as_cmd}` instead
of direct `$<TARGET_FILE:clang>` / `$<TARGET_FILE:llvm-as>`.

### `cmake/FindHalide_LLVM.cmake`
When LLVM is built from a monorepo with the WebAssembly backend enabled,
`ClangTargets.cmake` references LLD targets (`lldWasm`, `lldCommon`). If Clang
is found before LLD, CMake errors with "target lldWasm not found". Fix: added
`find_package(LLD)` before `find_package(Clang)` when `"WebAssembly" IN_LIST
LLVM_TARGETS_TO_BUILD`.

### `src/Target.cpp`
`calculate_host_target()` falls through to x86-specific CPU detection code
(cpuid, VendorSignatures, etc.) when no architecture `#ifdef` matches. Under
Emscripten, none of the arch defines (`__arm__`, `__aarch64__`, `__powerpc__`,
etc.) are set, so it hits the `#else` x86 block which doesn't compile for wasm.

Fix: Added `#elif defined(__EMSCRIPTEN__)` that returns `Target::WebAssembly`
arch with `Target::WebAssemblyRuntime` OS. Note: the enum is
`Target::WebAssemblyRuntime`, NOT `Target::WasmRT` (the string form "wasmrt"
maps to this in `os_name_map`).

### `src/Util.cpp`
Two issues:

1. **`run_with_large_stack()`** uses `getcontext`/`makecontext`/`swapcontext`
   (POSIX ucontext API). Emscripten's musl libc doesn't provide these. Fix:
   Added `__EMSCRIPTEN__` to the `MAKECONTEXT_OK` guard, so the function falls
   back to just calling the action directly (no stack switching).

2. **`run_process()`** uses `posix_spawnp` to spawn child processes. Not
   available in wasm. Fix: Added `#elif defined(__EMSCRIPTEN__)` that emits a
   `user_error` at runtime. This function is only called for `compile_to_file`
   with an external assembler or for running external tools — AOT bitcode
   generation doesn't need it.

### `tools/CMakeLists.txt`
Build-time tools (`build_halide_h`, `binary2cpp`, `regexp_replace`) are compiled
as wasm and run via `node` (the `CMAKE_CROSSCOMPILING_EMULATOR`). By default,
Emscripten provides a virtual filesystem — these tools can't read source files
from the host. Fix: Added `-sNODERAWFS=1` link option which gives node-hosted
wasm programs direct access to the host filesystem.

### `wasm/build.sh` (new)
Build orchestration script. Handles LLVM source detection, native tool
validation, Emscripten toolchain discovery. Supports `--skip-llvm`,
`--skip-halide`, `--run-test`, `--llvm-targets`, etc.

### `wasm/test_halide_wasm.cpp` (new)
Smoke test with three tests exercising the Halide compiler pipeline under wasm.

## Proven-Working Build Configuration

### Environment
- Emscripten SDK 5.0.2
- LLVM 20.1.2 source (for wasm LLVM build)
- Native LLVM 20 (Ubuntu packages: llvm-20, clang-20, lld-20)
- CMake 3.28+, Ninja

### Stage 1: LLVM for wasm32

```bash
emcmake cmake -S /opt/llvm-project/llvm -B /opt/llvm-wasm-build -G Ninja \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DLLVM_TARGETS_TO_BUILD="WebAssembly;X86" \
  -DLLVM_ENABLE_PROJECTS="clang;lld" \
  -DLLVM_TABLEGEN=/usr/lib/llvm-20/bin/llvm-tblgen \
  -DCLANG_TABLEGEN=/usr/lib/llvm-20/bin/clang-tblgen \
  -DLLVM_BUILD_TOOLS=OFF \
  -DLLVM_BUILD_UTILS=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_DOCS=OFF \
  -DLLVM_ENABLE_TERMINFO=OFF \
  -DLLVM_ENABLE_ZLIB=OFF \
  -DLLVM_ENABLE_ZSTD=OFF \
  -DLLVM_ENABLE_LIBXML2=OFF \
  -DLLVM_ENABLE_LIBEDIT=OFF \
  -DLLVM_ENABLE_LIBPFM=OFF \
  -DLLVM_ENABLE_THREADS=OFF \
  -DLLVM_ENABLE_PIC=OFF \
  -DLLVM_ENABLE_ASSERTIONS=OFF \
  -DLLVM_ENABLE_BACKTRACES=OFF \
  -DLLVM_ENABLE_CRASH_OVERRIDES=OFF \
  -DLLVM_ENABLE_UNWIND_TABLES=OFF \
  -DBUILD_SHARED_LIBS=OFF \
  -DCLANG_BUILD_TOOLS=OFF \
  -DCLANG_INCLUDE_TESTS=OFF \
  -DCLANG_INCLUDE_DOCS=OFF

cmake --build /opt/llvm-wasm-build -j$(nproc)
```

This builds 3715 targets, takes ~40 minutes on 4 cores, and produces 158 static
libraries totaling ~444 MB. Only WebAssembly and X86 backends were included in
this test run; the build script defaults to all backends.

### Stage 2: Halide for wasm32

```bash
emcmake cmake -S /home/user/Halide -B /home/user/Halide/build-halide-wasm -G Ninja \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DBUILD_SHARED_LIBS=OFF \
  -DLLVM_DIR=/opt/llvm-wasm-build/lib/cmake/llvm \
  -DClang_DIR=/opt/llvm-wasm-build/lib/cmake/clang \
  -DLLD_DIR=/opt/llvm-wasm-build/lib/cmake/lld \
  -DHalide_NATIVE_CLANG=/usr/lib/llvm-20/bin/clang \
  -DHalide_NATIVE_LLVM_AS=/usr/lib/llvm-20/bin/llvm-as \
  -DHalide_WASM_BACKEND=OFF \
  -DWITH_TESTS=OFF \
  -DWITH_TUTORIALS=OFF \
  -DWITH_AUTOSCHEDULERS=OFF \
  -DWITH_PYTHON_BINDINGS=OFF \
  -DWITH_UTILS=OFF \
  -DWITH_DOCS=OFF \
  -DWITH_SERIALIZATION=OFF \
  -DWITH_PACKAGING=OFF \
  -DCMAKE_CROSSCOMPILING_EMULATOR=node \
  -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
  -DVulkanHeaders_DIR=/home/user/Halide/dependencies/vulkan/share/cmake/VulkanHeaders

cmake --build build-halide-wasm -j$(nproc)
```

Key flags explained:
- `Halide_NATIVE_CLANG` / `Halide_NATIVE_LLVM_AS` — native tools for runtime bitcode
- `Halide_WASM_BACKEND=OFF` — disables wabt dependency (requires exceptions support, unrelated to wasm output target)
- `CMAKE_CROSSCOMPILING_EMULATOR=node` — run wasm build-time tools via node
- `CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH` — find packages in both sysroot and host paths
- `VulkanHeaders_DIR` — explicit path since Emscripten's find_package filtering hides host paths

### Stage 3: Building a test program

```bash
em++ -O2 -std=c++17 \
  -I/home/user/Halide/build-halide-wasm/include \
  -I/home/user/Halide/src \
  wasm/test_halide_wasm.cpp \
  -L/home/user/Halide/build-halide-wasm/src -lHalide \
  -L/opt/llvm-wasm-build/lib \
  -llldWasm -llldCommon \
  -lLLVMWebAssemblyCodeGen -lLLVMWebAssemblyAsmParser \
  -lLLVMWebAssemblyDisassembler -lLLVMWebAssemblyUtils \
  -lLLVMWebAssemblyDesc -lLLVMWebAssemblyInfo \
  -lLLVMX86CodeGen -lLLVMX86AsmParser -lLLVMX86Desc \
  -lLLVMX86Disassembler -lLLVMX86Info \
  -lLLVMOrcJIT -lLLVMExecutionEngine -lLLVMJITLink \
  -lLLVMOrcTargetProcess -lLLVMOrcShared -lLLVMRuntimeDyld \
  -lLLVMPasses -lLLVMCoroutines -lLLVMHipStdPar -lLLVMipo \
  -lLLVMLinker -lLLVMFrontendOpenMP -lLLVMFrontendOffloading \
  -lLLVMFrontendAtomic -lLLVMVectorize -lLLVMSandboxIR \
  -lLLVMAsmPrinter -lLLVMCFGuard -lLLVMGlobalISel \
  -lLLVMSelectionDAG -lLLVMCodeGen -lLLVMScalarOpts \
  -lLLVMAggressiveInstCombine -lLLVMInstCombine \
  -lLLVMObjCARCOpts -lLLVMCGData -lLLVMBitWriter \
  -lLLVMTarget -lLLVMIRPrinter -lLLVMInstrumentation \
  -lLLVMTransformUtils -lLLVMAnalysis -lLLVMProfileData \
  -lLLVMSymbolize -lLLVMDebugInfoDWARF -lLLVMDebugInfoPDB \
  -lLLVMObject -lLLVMIRReader -lLLVMBitReader \
  -lLLVMAsmParser -lLLVMCore -lLLVMRemarks \
  -lLLVMBitstreamReader -lLLVMTextAPI -lLLVMDebugInfoCodeView \
  -lLLVMDebugInfoMSF -lLLVMDebugInfoBTF -lLLVMMCParser \
  -lLLVMCodeGenTypes -lLLVMMCDisassembler -lLLVMMC \
  -lLLVMBinaryFormat -lLLVMTargetParser -lLLVMSupport \
  -lLLVMDemangle -lLLVMLTO -lLLVMExtensions \
  -lLLVMWindowsDriver -lLLVMOption \
  -o test_halide_wasm.js \
  -sALLOW_MEMORY_GROWTH=1 \
  -sINITIAL_MEMORY=536870912 \
  -sSTACK_SIZE=8388608 \
  -sNO_EXIT_RUNTIME=0 \
  -sENVIRONMENT=node

node test_halide_wasm.js
```

The long LLVM library list is the complete transitive dependency set. Order
matters (static linking). This could be improved by using a `.cmake` export or
`llvm-config --libs`.

## Issues Encountered and Resolved

| # | Error | Root Cause | Fix |
|---|-------|------------|-----|
| 1 | ClangConfig.cmake: target `lldWasm` not found | Monorepo ClangTargets.cmake references LLD targets | `find_package(LLD)` before `find_package(Clang)` in FindHalide_LLVM.cmake |
| 2 | VulkanHeaders not found | Emscripten's `CMAKE_FIND_ROOT_PATH_MODE_PACKAGE` filtering | Explicit `-DVulkanHeaders_DIR=...` and `-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH` |
| 3 | wabt build failures (exceptions) | wabt requires C++ exceptions, irrelevant to our wasm target | `-DHalide_WASM_BACKEND=OFF` |
| 4 | build_halide_h can't open LICENSE.txt | Emscripten virtual FS doesn't expose host files | `-sNODERAWFS=1` for build-time tools |
| 5 | x86 cpuid code in Target.cpp | `calculate_host_target()` assumes x86 in fallthrough `#else` | `#elif defined(__EMSCRIPTEN__)` guard |
| 6 | `Target::WasmRT` doesn't exist | Wrong enum name | Correct enum is `Target::WebAssemblyRuntime` |
| 7 | `posix_spawnp` undefined | Not available in Emscripten's musl | `#elif defined(__EMSCRIPTEN__)` with runtime error |
| 8 | `getcontext`/`makecontext` undefined | Not available in Emscripten's musl | Added `__EMSCRIPTEN__` to `MAKECONTEXT_OK` guard |

## Known Limitations and Future Work

### Not yet done
- **JIT compilation**: The ORC JIT engine links but won't work in wasm (no
  mmap/mprotect for executable pages). Would need a wasm-specific JIT backend
  or interpreter.
- **All LLVM backends**: Current test only includes WebAssembly + X86. The build
  script supports `--llvm-targets all` but it hasn't been tested yet. Adding all
  backends will increase binary size significantly.
- **`run_process()`**: Disabled with a runtime error. Any Halide code path that
  shells out to external tools won't work. This mainly affects
  `compile_to_file()` with certain output formats that invoke the system linker
  or assembler.
- **`run_with_large_stack()`**: Falls back to direct invocation (no stack
  switching). This means deeply recursive lowering passes could stack-overflow.
  The default wasm stack is 8MB which should be sufficient for most pipelines,
  but pathological cases may need `-sSTACK_SIZE=` increased.
- **Binary size**: ~45MB for a test program. Could be reduced with:
  - `-Oz` instead of `-O2`
  - `wasm-opt` post-processing
  - Fewer LLVM backends
  - LTO (though LLVM's LTO on LLVM itself is slow)
  - Stripping debug info (already MinSizeRel but LLVM is large)
- **Build script**: The `wasm/build.sh` script doesn't yet pass through the LLVM
  library list to Stage 3. The manual `em++` invocation with ~60 `-l` flags is
  fragile. Should use CMake for the test build too, or extract the link line from
  the Halide CMake build.
- **`Halide_WASM_BACKEND=OFF`**: We disabled the Halide wasm backend (wabt-based
  JIT executor) because wabt requires exceptions. The wasm *target* for AOT
  codegen still works fine (it uses LLVM's WebAssembly backend, not wabt).
  Re-enabling would require building wabt with `-fexceptions` or using
  Emscripten's exception support.
- **Browser support**: Currently only tested with Node.js (`-sENVIRONMENT=node`
  and NODERAWFS). For browser usage, would need:
  - Remove NODERAWFS dependency (use Emscripten's virtual FS or fetch API)
  - Add `-sENVIRONMENT=web` or `web,worker`
  - Provide output via in-memory buffers instead of files
  - Consider `-sMODULARIZE=1` for better JS integration

### Upstream considerations
- The `src/runtime/CMakeLists.txt` changes (native tool overrides) are clean and
  could be upstreamed independently — they're useful for any cross-compilation
  scenario, not just wasm.
- The `FindHalide_LLVM.cmake` LLD-before-Clang fix is also independently useful
  for anyone building Halide against a monorepo LLVM with WebAssembly support.
- The `src/Target.cpp` and `src/Util.cpp` changes are Emscripten-specific guards
  that are low-risk and could be upstreamed.
- The `tools/CMakeLists.txt` NODERAWFS change is specific to the Emscripten
  cross-compilation path.

## Prior Art

- **binji/aspect-clang**: Built Clang+LLVM for wasm, runs in the browser.
  Produced ~100MB wasm binaries. Demonstrates feasibility.
- **aspect-build/aspect-cli wasm-clang**: Maintained fork of the above.
- **aspect-build/aspect-cli Wasmer's clang-in-browser**: Similar concept, ~120MB.
- **aspect-build/aspect-cli aspect-build/aspect-cli aspect-build/aspect-cli**: Various projects have compiled LLVM to wasm for
  educational/playground purposes (Compiler Explorer, Godbolt, etc.)

## Directory Layout

```
Halide/
├── wasm/
│   ├── build.sh              # Multi-stage build orchestrator
│   └── test_halide_wasm.cpp  # Smoke test (3 tests)
├── build-halide-wasm/        # (not checked in) Halide wasm build output
│   ├── src/libHalide.a       # 39 MB wasm static library
│   ├── tools/gengen.wasm     # 44 MB generator runtime
│   └── include/Halide.h      # Generated uber-header
└── WASM_WIP.md               # This file
```

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository. Personal overrides (e.g. always preferring the
Makefile) belong in a git-ignored `CLAUDE.local.md` in the repo root, not here.

## What this is

Halide is a compiler embedded in C++ (with Python bindings) for writing
high-performance image/array processing pipelines. Users describe pipelines as
`Func`/`Var`/`Expr` algebra (front-end), which Halide lowers to an internal
`Stmt`/`Expr` IR, applies dozens of scheduling/optimization passes to, then
codegens via LLVM for CPU targets (x86, ARM, Hexagon, PowerPC, RISC-V, Wasm) or
emits device-C/shading code for GPU backends (CUDA, OpenCL, Metal, D3D12,
Vulkan, WebGPU).

## Building

CMake (3.28+) is the supported build system; the top-level `Makefile` also works
and is preferred by some contributors, but is unsupported for third parties
("use at your own risk", can't build Python bindings or packages). If instructed
to use the Makefile, don't forget to verify that any new tests are added to the
CMake build too before pushing.

Halide needs LLVM 21, 22, or 23 (trunk also works). Easiest way to get one is
via a `uv`-managed Python virtual env; activate it before building so CMake can
autodetect the LLVM install:

```shell
$ uv sync --group ci-llvm-22 --no-install-project
$ source .venv/bin/activate
```

Basic build:

```shell
$ cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build
```

If CMake can't find LLVM (e.g. the venv isn't active in the current shell), pass
it explicitly: `-DHalide_LLVM_ROOT=$(halide-llvm --prefix)` (run `halide-llvm`
from inside the venv to get the right absolute path).

On macOS with Homebrew LLVM, `cmake --preset=macOS -S . -B build` finds it
automatically. Other useful presets (see `CMakePresets.json`): `debug`,
`release`, `release-vcpkg`, `macOS-fuzz`, `macOS-coverage`, `linux-x64-asan`,
`linux-x64-fuzzer`, `valgrind`, `sde`.

Key build options (`Halide_feature(...)` in `CMakeLists.txt`): `WITH_TESTS`,
`WITH_AUTOSCHEDULERS`, `WITH_PYTHON_BINDINGS`, `WITH_SERIALIZATION`,
`WITH_TUTORIALS`, `WITH_UTILS`, `WITH_DOCS`. `Halide_ENABLE_MEMCHECK` enables
`ctest -T memcheck` (valgrind) runs.

Details: `doc/BuildingHalideWithCMake.md`. Package-consumer docs:
`doc/HalideCMakePackage.md`.

## Testing

Tests use CTest and live under `test/`, one CTest label per subdirectory
(`correctness`, `error`, `warning`, `generator`, `performance`, `runtime`,
`fuzz`, `integration`, `autoschedulers/$AS`). See `doc/Testing.md` for the full
breakdown of conventions (e.g. `error` tests expect an uncaught non-Halide
exception, `warning` tests look for a `Warning:` line, tests print `[SKIP]` when
inapplicable to the current target).

```shell
$ ctest --test-dir build --output-on-failure          # everything
$ ctest --test-dir build -L correctness               # one label
$ ctest --test-dir build -R correctness_bounds         # one test by name
```

Test binaries are named `<label>_<basename>`, e.g. `test/correctness/bounds.cpp`
builds `correctness_bounds`; running it directly (e.g. under `lldb`) works too.
Tests print `Success!` on success.

`make run_tests` and `make test_apps` are the make-based equivalents (JIT tests
and app compile/run checks, respectively).

Useful runtime env vars (see README "Some useful environment variables"):
`HL_JIT_TARGET`, `HL_TARGET` (AOT target for make-based test builds),
`HL_DEBUG_CODEGEN=1` (+ higher for more detail), `HL_NUM_THREADS`,
`HL_TRACE_FILE`.

## Linting / formatting

This repo uses `pre-commit`; run it locally before sending a PR:

```shell
$ pre-commit run --all-files
```

It runs `clang-format` (C++, sorted includes), `clang-tidy` (via
`run-clang-tidy.sh`), `ruff` (Python), `codespell`, `shellcheck`/`shfmt`,
`mdformat`, `taplo`, and CMake style/file-list checks
(`tools/check_cmake_style.py`, `tools/check_cmake_file_lists.py` — CMake test
`CMakeLists.txt` file lists are checked for being sorted/complete).

## Contribution norms (from CONTRIBUTING.md)

- Non-trivial changes should have a GitHub Issue/Discussion first to align on
  design before large effort is invested.
- Bug fixes need a regression test; new features need tests (fuzz tests
  requested where applicable).
- Public API changes (anything in `Halide::`) need doc updates and corresponding
  Python binding updates.
- Performance-affecting changes should describe expected impact / include
  benchmarks in the PR description.
- Keep unrelated fixes out of feature/bugfix PRs, even tempting drive-by CI
  fixes — open a separate PR.
- AI-assisted contributions require a `Co-authored-by:` trailer identifying the
  tool (see `CONTRIBUTING.md` for the exact format Halide expects).
- Documentation, comments, and tutorials should match the existing style and
  prose — keep explanations simple and concise. If a change makes any of these
  out of date, identify and update them to match the implementation.

## Common mistakes to avoid

- Comments should describe the code as it is now, not the history of how it got
  there — don't reference past bugs, prior implementations, or the current
  task/PR in comments.
- Prefer existing compiler machinery (`IRVisitor`/`IRMutator`/`IRMatch` helpers,
  existing `Simplify` rules, etc.) over hand-rolling a new mechanism that
  duplicates something already in the codebase.
- Only amend or force-push commits that haven't been pushed yet; once a commit
  is pushed, add new commits on top instead (PRs are squash-merged, so in-branch
  history doesn't need to be pristine).
- When benchmarking multiple things, run them sequentially, not in parallel —
  concurrent runs contend for CPU/cache resources and produce misleading
  numbers.

## Architecture

### Front end (embedded DSL)

The user-facing API — `Func`, `Var`, `Expr`, `Buffer`, `Param`, `RDom`,
`Pipeline`, scheduling directives (`.split`, `.vectorize`, `.compute_at`, etc.)
— lives in top-level `src/*.h`/`.cpp` files named after the concept (`Func.h`,
`Var.h`, `Schedule.h`, `RDom.h`, ...). This is the layer most user code and
tutorials interact with directly.

### IR core

`Expr`/`Stmt` (defined in `IR.h`/`Expr.h`) are the internal AST nodes lowered
pipelines are built from. Almost every pass in the compiler is one of:

- an `IRVisitor`/`IRGraphVisitor` (`IRVisitor.h`) — read-only traversal,
- an `IRMutator` (`IRMutator.h`) — traversal producing a rewritten IR, or
- pattern-matching via `IRMatch.h` (`Halide::IR::match`-style wildcard matching,
  heavily used in `Simplify*.cpp` and `FindIntrinsics.cpp`) built on
  `IROperator.h` helpers.

`IRMatch.h` and `Type.h` are code-size- and perf-sensitive (they're inlined into
every simplifier pass), so changes there need to be weighed for both
compile-time throughput and generated-binary size, not just correctness.

For a short, single-use traversal (no other caller needs the visitor/mutator
class), prefer the lambda-based `visit_with`/`mutate_with` helpers
(`IRVisitor.h`/`IRMutator.h`) over defining a full `IRVisitor`/`IRMutator`
subclass — pass one lambda per node type you care about
(`(auto *self, const T *op) { ... }`); unhandled node types fall through to the
default recursive behavior. Reach for a real subclass instead once the logic
needs to be reused from multiple call sites or carries enough per-pass state
that a named class is clearer.

### Lowering pipeline

`Lower.cpp` (`lower_main_stmt`/`lower`) is the orchestrator: it runs Funcs
through ~50-100 named passes in a fixed order — bounds inference (`Bounds.cpp`,
`BoundsInference.cpp`), inlining, storage flattening, loop partitioning,
vectorization/simplification, GPU loop handling (`FuseGPUThreadLoops.cpp`,
`CanonicalizeGPUVars.cpp`), async producer insertion, atomic mutex insertion,
and more — each implemented as its own `PassName.cpp`/`.h` pair at the top of
`src/`. When debugging an unexpected lowering result, `HL_DEBUG_CODEGEN=1`+
prints the IR after key passes, and `Lower.cpp`'s pass list is the map of "what
runs when."

### Code generation

`CodeGen_LLVM.cpp`/`.h` is the shared LLVM IR emission base class.
Architecture-specific specializations (`CodeGen_ARM.cpp`, `CodeGen_Hexagon.cpp`,
`CodeGen_RISCV.cpp`, `CodeGen_PowerPC.cpp`, `CodeGen_WebAssembly.cpp`, x86 via
`CodeGen_CPU.cpp`) override intrinsic lowering and target-specific codegen.
GPU/device backends are separate "Dev" codegen classes (`CodeGen_PTX_Dev.cpp`
for CUDA, `CodeGen_OpenCL_Dev.cpp`, `CodeGen_Metal_Dev.cpp`,
`CodeGen_D3D12Compute_Dev.cpp`, `CodeGen_Vulkan_Dev.cpp`,
`CodeGen_WebGPU_Dev.cpp`) that emit device kernels, while host-side glue for
offloading is handled by passes like `InjectHostDevBufferCopies.cpp` and
`OffloadGPULoops.cpp`. `CodeGen_C.cpp` is a separate C source backend (not
LLVM-based).

### Runtime

`src/runtime/` is a minimal, freestanding-ish C/C++ runtime (allocation,
threading, device API glue, tracing) compiled to LLVM bitcode and linked into
*every* compiled Halide pipeline. It must stay compilable without a full libc in
some configurations — notably, standalone Hexagon builds have no libc++, so
runtime headers must avoid pulling in heavy STL headers like `<iostream>` (use
`<ostream>`/`<iosfwd>` instead).

### Generators and AOT compilation

`Generator.h`/`AbstractGenerator.h` define the `Generator<T>` mechanism users
subclass to package a pipeline (`generate()`/`schedule()`) for ahead-of-time
compilation. `tools/GenGen.cpp` is the generic generator-executable `main()`
that CMake's `add_halide_generator`/`add_halide_library` (in
`cmake/HalideGeneratorHelpers.cmake`) build and invoke to emit object
files/headers. `Callable.h` covers a lighter-weight JIT-callable path that skips
the Generator plumbing.

### Autoschedulers

`src/autoschedulers/` contains pluggable autoscheduling algorithms
(`mullapudi2016`, `adams2019`, `li2018`, `anderson2021`), each built as its own
shared library that can be loaded at schedule time
(`Pipeline::apply_autoscheduler`) rather than linked into core Halide.
`src/autoschedulers/common` holds shared infrastructure.

### Python bindings

`python_bindings/` mirrors the C++ API via pybind11 (`src/`), with its own
`test/`, `tutorial/`, and `apps/` mirroring the top-level layout. Public API
changes in `Halide::` should come with a matching binding update here.

### apps/ and tutorial/

`apps/` holds larger, realistic example pipelines (camera_pipe, hannk,
resnet_50, local_laplacian, etc.) used as both examples and
performance/integration tests. `tutorial/` holds the numbered lessons behind
https://halide-lang.org/tutorials, each with corresponding code Halide expects
to keep in sync with the docs.

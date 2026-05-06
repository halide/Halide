# Coverage Collection Specification

## Objective

Measure and document the code coverage of the `src/` directory by the test suite
in `test/correctness`.

## Scope

- **Source code under analysis:** `src/`
- **Test suites:** `test/correctness`, `test/generator`, `test/error`,
  `test/autoschedulers`
- **Coverage metric:** Line and branch coverage

## Motivation

Understanding coverage helps identify gaps in the correctness test suite and
prioritize areas for additional testing or refactoring.

## Tools

- **Compiler:** Homebrew LLVM 21 (`/opt/homebrew/opt/llvm@21/bin/clang++`)
- **Coverage instrumentation:** Clang source-based coverage
  (`-fprofile-instr-generate -fcoverage-mapping`)
- **Profile merger:** `/opt/homebrew/opt/llvm@21/bin/llvm-profdata`
- **Report generator:** `/opt/homebrew/opt/llvm@21/bin/llvm-cov`

## CMake Presets

A `macOS-coverage` configure preset and matching test preset are defined in
`CMakePresets.json`. The configure preset inherits `macOS` and adds:

- `CMAKE_BUILD_TYPE=Debug` — maximizes line-number accuracy
- `-fprofile-instr-generate -fcoverage-mapping` on all compiler and linker flags
- `WITH_TEST_CORRECTNESS`, `WITH_TEST_GENERATOR`, `WITH_TEST_ERROR`, and
  `WITH_TEST_AUTO_SCHEDULE` enabled; tutorials, utils, and Python bindings
  disabled to reduce build time

The test preset sets `LLVM_PROFILE_FILE` to an absolute path so that profraw
files land in `build/macOS-coverage/profiles/` regardless of the test's working
directory.

## Running Coverage

The full workflow is automated by `tools/run-coverage.sh`:

```bash
tools/run-coverage.sh
```

The script:

1. Configures and builds the `macOS-coverage` preset.
2. Runs all correctness, generator, error, and autoscheduler tests with
   `HL_TARGET=host`, writing profraw files to `build/macOS-coverage/profiles/`.
3. Reconfigures with `Halide_TARGET=host-metal` and rebuilds, then runs only
   tests whose name contains `gpu` (with `HL_TARGET=host-metal`), appending
   profraw files to the same directory.
4. Restores the build directory to the host target.
5. Merges all profraw files and generates
   `build/macOS-coverage/coverage-report.txt`.

The `macOS-coverage` test preset (in `CMakePresets.json`) is also available for
manual runs without the full script:

```bash
mkdir -p build/macOS-coverage/profiles
ctest --preset macOS-coverage -j$(sysctl -n hw.logicalcpu)
```

## Results (collected 2026-05-06)

Summary across all `src/` files, driven by the four test suites:

| Metric    | Total  | Missed | Coverage   |
| --------- | ------ | ------ | ---------- |
| Regions   | 95,307 | 52,058 | 45.38%     |
| Functions | 7,388  | 1,717  | 76.76%     |
| Lines     | 96,978 | 28,189 | **70.93%** |
| Branches  | 45,420 | 18,170 | 60.00%     |

### Notable gaps (0% line coverage)

| File                        | Notes                                       |
| --------------------------- | ------------------------------------------- |
| `Deserialization.cpp`       | Pipeline serialization (1,356 lines)        |
| `Serialization.cpp`         | Pipeline serialization                      |
| `EliminateBoolVectors.cpp`  | GPU transform (not triggered by Metal path) |
| `CodeGen_OpenCL_Dev.cpp`    | OpenCL backend (no hardware)                |
| `CodeGen_Vulkan_Dev.cpp`    | Vulkan backend (no hardware)                |
| `CodeGen_WebGPU_Dev.cpp`    | WebGPU backend (no hardware)                |
| `SpirvIR.cpp`               | SPIR-V IR (Vulkan only)                     |
| `CPlusPlusMangle.cpp`       | C++ name mangling                           |
| `ExtractTileOperations.cpp` | Tile operation extraction                   |

### Low coverage (< 15%)

| File                           | Line Coverage |
| ------------------------------ | ------------- |
| `LowerWarpShuffles.cpp`        | 5.19%         |
| `CodeGen_D3D12Compute_Dev.cpp` | 8.29%         |
| `CodeGen_PTX_Dev.cpp`          | 8.67%         |
| `Generator.h`                  | 10.56%        |

### Well-covered (100% line coverage)

`IRMutator.cpp`, `Qualify.cpp`, `ParallelRVar.cpp`, `RemoveExternLoops.cpp`,
`UnifyDuplicateLets.cpp`, `UnpackBuffers.cpp`, `Var.cpp`, all `Simplify_*.cpp`
files, and several others.

## Notes

- One test fails on Apple Silicon: `correctness_simd_op_check_x86` (Bus error —
  x86 SIMD checks not runnable on arm64). All other tests pass or are skipped.
- The host-metal pass brought `CodeGen_Metal_Dev.cpp` from 0% to 85% and
  `FuseGPUThreadLoops.cpp` from 3% to 91%.
- OpenCL, Vulkan, WebGPU, D3D12, PTX, and SPIR-V backends remain near-zero — no
  corresponding hardware available on the test machine.
- `Deserialization.cpp` and `Serialization.cpp` are not exercised by any current
  test; a dedicated serialization test suite would be needed.
- The presets are committed in `CMakePresets.json` and available to all
  contributors.
- `llvm-cov` warnings are suppressed with `2>/dev/null`. The remaining warnings
  are about `HALIDE_ALWAYS_INLINE` and template helper functions in public
  headers that have coverage hash `0x0` (single basic block, no branches) and no
  standalone profraw record because they are always inlined at call sites. Their
  line coverage is captured through the callers, so the reported numbers are
  accurate. Use `--dump` with any `llvm-cov` subcommand to inspect them.
- `generator_aot_*` and `generator_aotcpp_*` binaries are excluded from the
  `-object` list. At test runtime they execute pre-compiled Halide pipelines and
  add no unique `src/` coverage. Including them with stale hashes (after the
  `host-metal` rebuild) would incorrectly count their functions as uncovered,
  deflating reported coverage by ~4 percentage points.

# Fuzz testing

Halide has a set of fuzz-testing harnesses in `test/fuzz/` that can find tricky
edge cases and bugs that are hard to catch with a regular unit-testing suite.
The fuzz tests are built on a small in-tree framework (`fuzz_helpers.h`,
`halide_fuzz_main.h`) that abstracts over two backends:

- **stdlib backend** — uses `std::mt19937_64` seeded from `std::random_device`.
  Works with any standard C++ toolchain; no special compiler flags or external
  runtime libraries required. Each run prints its seed so failures are
  reproducible by re-running with that seed.
- **libfuzzer backend** — uses
  [libFuzzer](https://www.llvm.org/docs/LibFuzzer.html) for coverage-guided
  fuzzing. Requires a Clang toolchain built with `-fsanitize=fuzzer` support.

The stdlib backend is the default for regular development builds. The libfuzzer
backend is enabled automatically when the build system detects
`-fsanitize=fuzzer` (or equivalent) linker flags.

## Building fuzz tests

### Standard build (stdlib backend)

No special flags are needed. The fuzz tests build as part of any normal CMake
configuration that has `WITH_TEST_FUZZ=YES`:

```
cmake -B build <your-usual-options> -DWITH_TEST_FUZZ=YES
cmake --build build -j$(nproc) --target test_fuzz
```

### libfuzzer backend (coverage-guided fuzzing)

Use one of the fuzzing CMake presets, which set the necessary
`-fsanitize=fuzzer[-no-link]` flags across the entire build:

**Linux:**

```
cmake -B build --preset linux-x64-fuzzer -DHalide_LLVM_ROOT=/path/to/llvm-install
cmake --build build -j$(nproc)
```

**macOS (Homebrew LLVM):**

```
cmake -B build --preset macOS-fuzz
cmake --build build -j$(nproc)
```

The LLVM install used for libfuzzer builds must include the `compiler-rt`
runtime (i.e. built with `-DLLVM_ENABLE_RUNTIMES="compiler-rt"`). Not all
prebuilt LLVM installs include this; you may need to build LLVM from source or
use Homebrew's LLVM package on macOS.

## Running fuzz tests

### stdlib backend

Run a fuzz harness directly:

```
./build/test/fuzz/fuzz_simplify
```

By default this runs 10,000 iterations, printing the seed before each one:

```
Seed: 12345678901234567
Seed: 98765432109876543
...
```

Control the number of iterations with `-runs=N`:

```
./build/test/fuzz/fuzz_simplify -runs=100000
```

Run all fuzz tests via CTest (1,000 iterations each, exit-code–based pass/fail):

```
ctest --test-dir build -L fuzz
```

### libfuzzer backend

After building with a fuzzing preset, run the harness with no arguments to start
coverage-guided fuzzing on a single core:

```
./build/test/fuzz/fuzz_simplify
```

To persist the corpus between runs (recommended):

```
mkdir -p fuzz_simplify_corpus
./build/test/fuzz/fuzz_simplify fuzz_simplify_corpus
```

To fuzz in parallel across all available cores:

```
./build/test/fuzz/fuzz_simplify fuzz_simplify_corpus -fork=$(nproc)
```

## Reproducing failures

### stdlib backend

When a run fails, rerun with the seed that was printed just before the crash:

```
./build/test/fuzz/fuzz_simplify 12345678901234567
```

This performs a single deterministic iteration with that seed.

### libfuzzer backend

libFuzzer writes a crash-input file on failure:

```
crash-<some_random_hash>
```

Replay it by passing it as the first argument:

```
./build/test/fuzz/fuzz_simplify crash-<some_random_hash>
```

## Adding new fuzz tests

All fuzz tests use the `FUZZ_TEST` macro defined in `fuzz_helpers.h`. This macro
generates the correct entry point for whichever backend is active —
`LLVMFuzzerTestOneInput` for libfuzzer or a `main` that calls
`Halide::fuzz_main` for the stdlib backend.

A minimal fuzz test looks like this:

```cpp
#include "fuzz_helpers.h"

FUZZ_TEST(my_test, Halide::FuzzingContext &fuzz) {
    int x = fuzz.ConsumeIntegralInRange<int>(0, 100);
    bool b = fuzz.ConsumeBool();
    my_function(x, b);
    return 0;
}
```

`FuzzingContext` wraps `FuzzedDataProvider` (from libfuzzer's
`compiler-rt/include/fuzzer/FuzzedDataProvider.h`) and re-implements its
interface on top of `std::mt19937_64` for the stdlib backend, so the same API
works with both backends. Key methods:

- `ConsumeIntegral<T>()` — random value of type `T`
- `ConsumeIntegralInRange<T>(min, max)` — random value in `[min, max]`
- `ConsumeBool()` — random boolean
- `PickValueInArray(arr)` — random element from an array or initializer list
- `PickValueInVector(vec)` — random element from a `std::vector`

For richer examples, see `test/fuzz/simplify.cpp` and
`test/fuzz/random_expr_generator.h`.

To register a new fuzz test with CMake, add it to the `SOURCES` list in
`test/fuzz/CMakeLists.txt`.

## Other useful materials

- [The official libfuzzer docs](https://www.llvm.org/docs/LibFuzzer.html)
- [The libfuzzer tutorial](https://github.com/google/fuzzing/blob/master/tutorial/libFuzzerTutorial.md)
- [FuzzedDataProvider reference](https://github.com/llvm/llvm-project/blob/main/compiler-rt/include/fuzzer/FuzzedDataProvider.h)

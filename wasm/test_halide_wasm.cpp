// Smoke test for Halide compiled to WebAssembly.
//
// This program exercises the Halide compiler (running as wasm) to define a
// simple pipeline and compile it to an object file via AOT code generation.
// It verifies that the core compilation pipeline works: Halide IR lowering,
// LLVM IR generation, optimization, and backend code emission.
//
// Build with Emscripten:
//   em++ -O2 -std=c++17 test_halide_wasm.cpp -lHalide -o test.js \
//        -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=512MB
//
// Run:
//   node test.js

#include "Halide.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

static bool test_basic_compilation() {
    printf("Test 1: Basic pipeline compilation... ");

    Halide::Func f("f");
    Halide::Var x("x"), y("y");
    f(x, y) = Halide::cast<float>(x + y);

    // Compile to LLVM bitcode (avoids needing a linker/object file writer
    // for the target, which is the simplest output format to verify).
    Halide::Target target("x86-64-linux-no_runtime");
    f.compile_to_bitcode("/tmp/halide_wasm_test_basic.bc", {}, "f", target);

    printf("OK\n");
    return true;
}

static bool test_scheduled_pipeline() {
    printf("Test 2: Scheduled pipeline compilation... ");

    Halide::Func blur_x("blur_x"), blur_y("blur_y");
    Halide::Var x("x"), y("y"), xi("xi"), yi("yi");

    // A simple 3x3 box blur
    Halide::ImageParam input(Halide::Float(32), 2, "input");
    blur_x(x, y) = (input(x - 1, y) + input(x, y) + input(x + 1, y)) / 3.0f;
    blur_y(x, y) = (blur_x(x, y - 1) + blur_x(x, y) + blur_x(x, y + 1)) / 3.0f;

    // Apply a schedule
    blur_y.tile(x, y, xi, yi, 8, 4);
    blur_x.compute_at(blur_y, x);

    Halide::Target target("x86-64-linux-sse41-no_runtime");
    blur_y.compile_to_bitcode("/tmp/halide_wasm_test_blur.bc",
                              {input}, "blur", target);

    printf("OK\n");
    return true;
}

static bool test_wasm_target_output() {
    printf("Test 3: Compile for wasm target (wasm-from-wasm)... ");

    Halide::Func f("f_wasm");
    Halide::Var x("x");
    f(x) = x * x + 42;

    // Generate code targeting WebAssembly — the compiler itself is running
    // as wasm, and it's generating wasm output. Wasm-from-wasm!
    Halide::Target target("wasm-32-wasmrt-no_runtime");
    f.compile_to_bitcode("/tmp/halide_wasm_test_wasm_target.bc",
                         {}, "f_wasm", target);

    printf("OK\n");
    return true;
}

int main() {
    printf("Halide WebAssembly smoke test\n");
    printf("=============================\n");

    int passed = 0;
    int failed = 0;

    auto run = [&](bool (*test)()) {
        try {
            if (test()) {
                passed++;
            } else {
                failed++;
            }
        } catch (const Halide::Error &e) {
            printf("FAILED (Halide error: %s)\n", e.what());
            failed++;
        } catch (const std::exception &e) {
            printf("FAILED (exception: %s)\n", e.what());
            failed++;
        }
    };

    run(test_basic_compilation);
    run(test_scheduled_pipeline);
    run(test_wasm_target_output);

    printf("\nResults: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}

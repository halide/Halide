#include "Halide.h"
#include <stdio.h>

using namespace Halide;

extern "C" int my_extern_func(halide_buffer_t *input, halide_buffer_t *output) {
    if (input->is_bounds_query()) {
        input->dim[0].min = output->dim[0].min;
        input->dim[0].extent = output->dim[0].extent;
    } else {
        int *src = (int *)input->host;
        int *dst = (int *)output->host;
        for (int i = 0; i < output->dim[0].extent; i++) {
            dst[i] = src[i] + 1;
        }
    }
    return 0;
}

bool test_issue_1_extern_funcs() {
    Func f("f"), g("g");
    Var x("x");

    f(x) = x;

    std::vector<ExternFuncArgument> args;
    args.push_back(f);

    g.define_extern("my_extern_func", args, Int(32), 1);

    f.compute_root();

    Target t("wasm-32-wasmrt");

    Pipeline p(g);

    JITExtern extern_fn(my_extern_func);
    p.set_jit_externs({{"my_extern_func", extern_fn}});

    Buffer<int> output = p.realize({10}, t);

    if (output(5) != 6) {
        printf("Issue 1: Execution value error! output(5) = %d (expected 6)\n", output(5));
        return false;
    }

    printf("Issue 1 success!\n");
    return true;
}

bool test_issue_2_allocation_sizes() {
    Func f("f");
    Var x("x");

    f(x) = x;

    Target t("wasm-32-wasmrt");

    Pipeline p(f);

    // Moderate allocation size: 48KB image output buffer
    constexpr int kSize = 49152 / sizeof(int);  // 48KB
    Buffer<int> output = p.realize({kSize}, t);

    printf("Issue 2 success!\n");
    return true;
}

int main(int argc, char **argv) {
    if (!Halide::Internal::WasmModule::can_jit_target(Target("wasm-32-wasmrt"))) {
        printf("[SKIP] WebAssembly JIT executor not supported in this configuration.\n");
        return 0;
    }

    if (!test_issue_1_extern_funcs()) {
        return 1;
    }

    if (!test_issue_2_allocation_sizes()) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}

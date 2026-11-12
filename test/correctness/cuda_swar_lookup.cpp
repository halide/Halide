#include "Halide.h"
#include <cstdio>
#include <regex>

using namespace Halide;

// A lookup of 8-bit lanes in a small table of constants (a mux of them) in a
// loop vectorized with VectorizeIn::SWAR is done in a general-purpose
// register: on CUDA, byte permutes (prmt) of the table's words, one to three
// per 4 lanes, where a SIMD-vectorized lookup extracts each lane. Here a
// table of `n` signed values indexed by bits of the input (as GGML's iq4_nl
// codebook by a nibble of its codes), `vec` lanes a thread.
int test(const Target &t, int n, int vec) {
    std::vector<int8_t> values(n);
    for (int i = 0; i < n; i++) {
        values[i] = (int8_t)(i * 37 - 127);
    }
    std::vector<Expr> table;
    for (int8_t v : values) {
        table.push_back(cast<int8_t>(v));
    }
    Func in, f;
    Var x, xo, xi;
    in(x) = cast<uint8_t>(x * 29 + 5);
    in.compute_root();
    // (n = 12: an index in [0, 11] of two of the input's bit fields)
    Expr index = n == 12 ? in(x) % 8 + in(x) / 128 * 4 : in(x) % n;
    f(x) = mux(index, table) + cast<int8_t>(x);
    f.gpu_tile(x, xo, xi, 64 * vec).vectorize(xi, vec, VectorizeIn::SWAR);
    Buffer<int8_t> out = f.realize({1024 * vec}, t);
    out.copy_to_host();
    for (int i = 0; i < out.width(); i++) {
        const uint8_t byte = (uint8_t)(i * 29 + 5);
        const int8_t correct = (int8_t)(values[n == 12 ? byte % 8 + byte / 128 * 4 : byte % n] + (int8_t)i);
        if (out(i) != correct) {
            printf("%d entries, %d lanes: out(%d) = %d instead of %d\n", n, vec, i, out(i), correct);
            return 1;
        }
    }
    Buffer<uint8_t> buf = f.compile_to_module({}, "f", t).compile_to_buffer();
    const std::string src((const char *)buf.begin(), (const char *)buf.end());
    if (!std::regex_search(src, std::regex("prmt[.]b32"))) {
        printf("%d entries, %d lanes: the PTX permutes no bytes. Rerun with HL_DEBUG_CODEGEN=1 to debug\n", n, vec);
        return 1;
    }
    if (std::regex_search(src, std::regex("(ld|st)[.]local"))) {
        printf("%d entries, %d lanes: the PTX indexes the table in local memory. Rerun with HL_DEBUG_CODEGEN=1 to debug\n", n, vec);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        printf("[SKIP] Cuda is not enabled in target: %s\n", t.to_string().c_str());
        return 0;
    }
    for (int n : {4, 8, 12, 16}) {
        for (int vec : {4, 8}) {
            if (test(t, n, vec)) {
                return 1;
            }
        }
    }
    printf("Success!\n");
    return 0;
}

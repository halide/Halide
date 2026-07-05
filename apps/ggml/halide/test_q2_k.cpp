// Standalone round-trip check: the from-scratch Halide Q2_K dequantize
// kernel vs GGML's own reference implementation, reached via the public
// ggml_get_type_traits() API. Quantize here is scaffolding that itself
// calls out to GGML's reference (see ggml_extern_quantize.cpp), so its
// output is trivially identical to GGML's -- this test's real purpose is
// exercising the from-scratch dequantize implementation against blocks
// GGML itself produced.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <ggml.h>

#include "compare.h"
#include "data_gen.h"
#include "ggml_quants.h"

int main() {
    const int64_t k = 4096;  // multiple of QK_K (256)

    std::vector<float> x(k);
    generate_synthetic_data(x.data(), k);

    const ggml_type_traits *tt = ggml_get_type_traits(GGML_TYPE_Q2_K);
    const size_t out_bytes = ggml_row_size(GGML_TYPE_Q2_K, k);

    std::vector<uint8_t> ref_blocks(out_bytes), halide_blocks(out_bytes);
    tt->from_float_ref(x.data(), ref_blocks.data(), k);
    ggml_quants_halide_quantize_q2_k(x.data(), halide_blocks.data(), k);

    if (std::memcmp(ref_blocks.data(), halide_blocks.data(), out_bytes) != 0) {
        std::fprintf(stderr, "FAIL: quantize output does not match GGML's reference byte-for-byte\n");
        return 1;
    }

    std::vector<float> ref_y(k), halide_y(k);
    tt->to_float(ref_blocks.data(), ref_y.data(), k);
    ggml_quants_halide_dequantize_q2_k(ref_blocks.data(), halide_y.data(), k);

    if (!floats_match(ref_y.data(), halide_y.data(), k)) {
        std::fprintf(stderr, "FAIL: dequantize output does not match GGML's reference within tolerance\n");
        return 1;
    }

    std::printf("Success!\n");
    return 0;
}

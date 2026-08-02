// Standalone check: the from-scratch Halide IQ1_M dequantize kernel vs
// GGML's own reference implementation. GGML has no public from_float_ref
// for this importance-matrix-only codebook type (see
// ../providers/ggml_internal_abi.h's quantize_iq1_m doc comment) -- its
// only quantizer is reached the same way ggml_provider.cpp reaches it: the
// private whole-matrix quantize_iq1_m symbol, with a uniform (all-1.0)
// weighting for consistency with IQ2_XXS/IQ2_XS/IQ1_S (whose equivalent
// weights are a hard requirement, not just optional here).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <ggml.h>

#include "../providers/ggml_internal_abi.h"
#include "compare.h"
#include "data_gen.h"
#include "ggml_quants.h"

int main() {
    const int64_t k = 4096;  // multiple of QK_K (256)

    std::vector<float> x(k);
    generate_synthetic_data(x.data(), k);

    ggml_quantize_init(GGML_TYPE_IQ1_M);

    const ggml_type_traits *tt = ggml_get_type_traits(GGML_TYPE_IQ1_M);
    const size_t out_bytes = ggml_row_size(GGML_TYPE_IQ1_M, k);

    std::vector<float> weights(k, 1.0f);
    std::vector<uint8_t> ref_blocks(out_bytes);
    quantize_iq1_m(x.data(), ref_blocks.data(), /*nrows=*/1, /*n_per_row=*/k, weights.data());

    std::vector<float> ref_y(k), halide_y(k);
    tt->to_float(ref_blocks.data(), ref_y.data(), k);
    ggml_quants_halide_dequantize_iq1_m(ref_blocks.data(), halide_y.data(), k);

    if (!floats_match(ref_y.data(), halide_y.data(), k)) {
        std::fprintf(stderr, "FAIL: dequantize output does not match GGML's reference within tolerance\n");
        return 1;
    }

    std::printf("Success!\n");
    return 0;
}

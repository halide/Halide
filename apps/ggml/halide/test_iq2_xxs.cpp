// Standalone check: the from-scratch Halide IQ2_XXS dequantize kernel vs
// GGML's own reference implementation. GGML has no public from_float_ref
// for this importance-matrix-only codebook type (see
// ../providers/ggml_internal_abi.h's quantize_iq2_xxs doc comment) -- its
// only quantizer is reached the same way ggml_provider.cpp reaches it: the
// private whole-matrix quantize_iq2_xxs symbol, called with nrows=1 and no
// importance matrix to get a plain per-row reference.

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

    // The nearest-neighbor grid search this quantizer uses needs its lookup
    // table built first (see ggml_provider.cpp's comment on ggml_quantize_init).
    ggml_quantize_init(GGML_TYPE_IQ2_XXS);

    const ggml_type_traits *tt = ggml_get_type_traits(GGML_TYPE_IQ2_XXS);
    const size_t out_bytes = ggml_row_size(GGML_TYPE_IQ2_XXS, k);

    // quantize_row_iq2_xxs_impl hard-requires a non-null quant_weights
    // (GGML_ASSERT) -- a uniform (all-1.0) weighting treats every element
    // as equally important, the closest equivalent to "no weighting".
    std::vector<float> weights(k, 1.0f);
    std::vector<uint8_t> ref_blocks(out_bytes);
    quantize_iq2_xxs(x.data(), ref_blocks.data(), /*nrows=*/1, /*n_per_row=*/k, weights.data());

    std::vector<float> ref_y(k), halide_y(k);
    tt->to_float(ref_blocks.data(), ref_y.data(), k);
    ggml_quants_halide_dequantize_iq2_xxs(ref_blocks.data(), halide_y.data(), k);

    if (!floats_match(ref_y.data(), halide_y.data(), k)) {
        std::fprintf(stderr, "FAIL: dequantize output does not match GGML's reference within tolerance\n");
        return 1;
    }

    std::printf("Success!\n");
    return 0;
}

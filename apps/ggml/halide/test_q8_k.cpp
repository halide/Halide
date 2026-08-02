// Standalone check: the from-scratch Halide Q8_K quantize kernel vs GGML's
// own reference implementation. Unlike every other type here, Q8_K has no
// public from_float_ref (see include/ggml.h's type_traits table) -- it's
// reached the same way apps/ggml/providers/ggml_provider.cpp reaches it,
// through the private quantize_row_q8_K_generic symbol declared in
// ../providers/ggml_internal_abi.h (see that header's own comment for why
// this one symbol needs the private ABI). Q8_K is activation-only, so
// there's no dequantize round-trip to check, only the quantize output.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <ggml.h>

#include "../providers/ggml_internal_abi.h"
#include "data_gen.h"
#include "ggml_quants.h"

int main() {
    const int64_t k = 4096;  // multiple of QK_K (256)

    std::vector<float> x(k);
    generate_synthetic_data(x.data(), k);

    const size_t out_bytes = ggml_row_size(GGML_TYPE_Q8_K, k);

    std::vector<uint8_t> ref_blocks(out_bytes), halide_blocks(out_bytes);
    quantize_row_q8_K_generic(x.data(), ref_blocks.data(), k);
    ggml_quants_halide_quantize_q8_k(x.data(), halide_blocks.data(), k);

    if (std::memcmp(ref_blocks.data(), halide_blocks.data(), out_bytes) != 0) {
        std::fprintf(stderr, "FAIL: quantize output does not match GGML's reference byte-for-byte\n");
        return 1;
    }

    std::printf("Success!\n");
    return 0;
}

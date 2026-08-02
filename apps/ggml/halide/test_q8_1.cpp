// Standalone check: the from-scratch Halide Q8_1 quantize kernel vs GGML's
// own reference implementation, reached via the public
// ggml_get_type_traits() API. Q8_1 is an activation-only format -- GGML has
// no public to_float for it, so there's no dequantize round-trip to check
// here, only the quantize output.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <ggml.h>

#include "data_gen.h"
#include "ggml_quants.h"

int main() {
    const int64_t k = 4096;  // multiple of QK8_1 (32)

    std::vector<float> x(k);
    generate_synthetic_data(x.data(), k);

    const ggml_type_traits *tt = ggml_get_type_traits(GGML_TYPE_Q8_1);
    const size_t out_bytes = ggml_row_size(GGML_TYPE_Q8_1, k);

    std::vector<uint8_t> ref_blocks(out_bytes), halide_blocks(out_bytes);
    tt->from_float_ref(x.data(), ref_blocks.data(), k);
    ggml_quants_halide_quantize_q8_1(x.data(), halide_blocks.data(), k);

    if (std::memcmp(ref_blocks.data(), halide_blocks.data(), out_bytes) != 0) {
        std::fprintf(stderr, "FAIL: quantize output does not match GGML's reference byte-for-byte\n");
        return 1;
    }

    std::printf("Success!\n");
    return 0;
}

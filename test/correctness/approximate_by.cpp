#include "Halide.h"
#include <cmath>
#include <cstdio>

using namespace Halide;

namespace {

constexpr int kBlockSize = 8;

// A minimal symmetric integer quantizer -- self-contained (no relation to
// any specific real-world format), just enough to exercise: encode()
// returning multiple Funcs plus a genuine scheduling-only handle (the
// per-block amax reduction), decode() combining them back into a single
// Func matching the original's signature, and approximate_by()'s eager
// substitution.
class SymmetricQuantizer : public Approximation {
public:
    EncodeResult encode(std::vector<Func> inputs) override {
        Func f = inputs[0];
        Var x("x"), i("i");
        RDom r(0, kBlockSize, "r");

        Func amax("amax");
        amax(i) = 0.0f;
        amax(i) = max(amax(i), abs(f(i * kBlockSize + r)));

        Func d("d");
        d(i) = amax(i) / 127.0f;

        Func q("q");
        Expr id = select(d(x / kBlockSize) != 0.0f, 1.0f / d(x / kBlockSize), 0.0f);
        q(x) = cast<int8_t>(clamp(round(f(x) * id), -127, 127));

        return {{q, d}, {amax}};
    }

    DecodeResult decode(std::vector<Func> encoded) override {
        Func q = encoded[0], d = encoded[1];
        Var x("x");
        Func dequantized("dequantized");
        dequantized(x) = cast<float>(q(x)) * d(x / kBlockSize);
        return {{dequantized}, {}};
    }
};

}  // namespace

int main(int argc, char **argv) {
    Var x("x");

    Func f("f");
    f(x) = sin(cast<float>(x) * 0.1f) * 100.0f;

    // g is rewired by approximate_by() below; h is not, and must keep
    // seeing the exact, unquantized f.
    Func g("g");
    g(x) = f(x) * 2.0f + 1.0f;

    Func h("h");
    h(x) = f(x) * 3.0f;

    SymmetricQuantizer quant;
    ApproximationResult result = f.approximate_by(quant, {g});

    if (result.handles.empty()) {
        printf("Expected approximate_by() to return scheduling handles\n");
        return 1;
    }
    result.replacement.compute_root();
    for (Func handle : result.handles) {
        handle.compute_root();
    }

    const int kSize = 64;
    Buffer<float> g_out = g.realize({kSize});
    Buffer<float> h_out = h.realize({kSize});

    for (int i = 0; i < kSize; i++) {
        const float fx = sinf(i * 0.1f) * 100.0f;

        // Independently recompute the same per-block quantization encode()
        // performs, to build a bit-exact reference for what g should see.
        const int block = i / kBlockSize;
        float amax = 0.0f;
        for (int j = 0; j < kBlockSize; j++) {
            const float v = sinf((block * kBlockSize + j) * 0.1f) * 100.0f;
            amax = std::max(amax, std::fabs(v));
        }
        const float d = amax / 127.0f;
        const float id = d != 0.0f ? 1.0f / d : 0.0f;
        float q = std::round(fx * id);
        q = std::max(-127.0f, std::min(127.0f, q));
        const float dequantized = q * d;

        const float expected_g = dequantized * 2.0f + 1.0f;
        if (std::fabs(g_out(i) - expected_g) > 1e-4f) {
            printf("g(%d) = %f, expected %f -- approximate_by's substitution did not take effect\n",
                   i, g_out(i), expected_g);
            return 1;
        }

        // h was never passed as a consumer to approximate_by(): it must
        // see the real f, not the quantized round trip.
        const float expected_h = fx * 3.0f;
        if (std::fabs(h_out(i) - expected_h) > 1e-4f) {
            printf("h(%d) = %f, expected %f -- approximate_by affected a Func not in `consumers`\n",
                   i, h_out(i), expected_h);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}

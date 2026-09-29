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
struct SymmetricQuantizer {
    EncodeResult encode(std::vector<Func> inputs) const {
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

    DecodeResult decode(std::vector<Func> encoded) const {
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

    // Stage tracing preserves opaque identities through nested combinators,
    // including repeated component types and Apply/TrustedInverse ownership.
    Approximation first_identity = Identity{}, second_identity = Identity{};
    Approximation trusted_encoder = Identity{}, trusted_decoder = Identity{};
    Approximation applied = Apply(0, second_identity);
    Approximation trusted = TrustedInverse(trusted_encoder, trusted_decoder);
    Approximation nested = Compose(first_identity, applied, trusted);

    // A copy of a handle is the same stage; a second conversion is not.
    Approximation first_copy = first_identity;
    if (!first_copy.same_as(first_identity) || first_identity.same_as(second_identity) ||
        Approximation().defined() || !nested.defined()) {
        printf("Approximation handle identity semantics are wrong\n");
        return 1;
    }

    Func traced_source("traced_source"), traced_consumer("traced_consumer");
    traced_source(x) = cast<float>(x);
    traced_consumer(x) = traced_source(x);
    ApproximationResult traced = traced_source.approximate_by(nested, {traced_consumer});

    auto require_port = [&](const Approximation &stage, const char *label) {
        if (!traced.encoded_by(stage).defined() || !traced.decoded_by(stage).defined()) {
            printf("Missing encode/decode stage trace for %s\n", label);
            return false;
        }
        return true;
    };
    if (!require_port(first_identity, "first repeated Identity") ||
        !require_port(second_identity, "second repeated Identity") ||
        !require_port(first_copy, "copy of first Identity") ||
        !require_port(applied, "Apply") ||
        !require_port(trusted, "TrustedInverse") ||
        !require_port(nested, "outer Compose")) {
        return 1;
    }
    // Looking up a stage that wasn't invoked in a direction is an error, so
    // check direction-specificity against the raw records instead.
    auto recorded = [](const std::vector<ApproximationStageOutputs> &outputs, const Approximation &stage) {
        for (const ApproximationStageOutputs &o : outputs) {
            if (o.stage.same_as(stage)) {
                return true;
            }
        }
        return false;
    };
    if (!recorded(traced.encoded_stage_outputs, trusted_encoder) ||
        recorded(traced.decoded_stage_outputs, trusted_encoder) ||
        recorded(traced.encoded_stage_outputs, trusted_decoder) ||
        !recorded(traced.decoded_stage_outputs, trusted_decoder)) {
        printf("TrustedInverse did not preserve direction-specific child traces\n");
        return 1;
    }
    Approximation unused = Identity{};
    if (recorded(traced.encoded_stage_outputs, unused) ||
        recorded(traced.decoded_stage_outputs, unused)) {
        printf("Unused stage unexpectedly recorded\n");
        return 1;
    }
    if (traced.encoded_by(first_identity, 1).defined() ||
        traced.decoded_by(first_identity, 1).defined()) {
        printf("Out-of-range port unexpectedly resolved\n");
        return 1;
    }

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

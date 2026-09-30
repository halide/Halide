#include "Halide.h"
#include <cmath>
#include <cstdio>

using namespace Halide;

namespace {

constexpr int kBlockSize = 8;

// A minimal symmetric integer quantizer -- self-contained (no relation to
// any specific real-world format), just enough to exercise: encode()
// returning multiple Funcs plus a genuine scheduling-only intermediate (the
// per-block amax reduction), decode() combining them back into a single
// Func matching the original's signature, and approximate_by()'s eager
// substitution.
struct SymmetricQuantizer {
    std::vector<Func> encode(const std::vector<Func> &inputs) const {
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

        return {q, d};
    }

    std::vector<Func> decode(const std::vector<Func> &encoded) const {
        Func q = encoded[0], d = encoded[1];
        Var x("x");
        Func dequantized("dequantized");
        dequantized(x) = cast<float>(q(x)) * d(x / kBlockSize);
        return {dequantized};
    }
};

// A single-form unit whose intermediates are only discovered.
struct TwoStep {
    std::string tag = "";

    Func encode(const Func &in) const {
        Var x("x");
        Func a("step_a" + tag), b("step_b" + tag);
        a(x) = in(x) + 1;
        b(x) = a(x) * 2;
        return b;
    }
    Func decode(const Func &in) const {
        Var x("x");
        Func out("step_out");
        out(x) = in(x) / 2 - 1;
        return out;
    }
};

int check_discovery() {
    Var x("x");
    // f has a producer; neither f nor g may show up as an intermediate.
    Func g("disc_g"), f("disc_f"), c("disc_c");
    g(x) = x;
    f(x) = g(x) * 2;
    c(x) = f(x);
    ApproximationResult r = f.approximate_by(Approximation(TwoStep{}), {c});
    std::vector<std::string> names;
    for (const Func &i : r.intermediates) {
        names.push_back(i.name());
        if (i.name() == "disc_f" || i.name() == "disc_g" || i.name() == r.replacement.name()) {
            printf("Intermediates contain an excluded Func: %s\n", i.name().c_str());
            return 1;
        }
    }
    // encoded (step_b) first, then the discovered step_a.
    if (names.size() != 2 || names[0].rfind("step_b", 0) != 0 || names[1].rfind("step_a", 0) != 0) {
        printf("Unexpected intermediates for TwoStep\n");
        return 1;
    }
    for (Func i : r.intermediates) {
        i.compute_root();
    }
    r.replacement.compute_root();
    Buffer<int> out = c.realize({8});
    for (int i = 0; i < 8; i++) {
        if (out(i) != i * 2) {
            printf("TwoStep round trip wrong at %d\n", i);
            return 1;
        }
    }

    // Topological order: a producer precedes its consumer.
    Approximation two = TwoStep{};
    EncodeResult e = two.encode({f});
    Func in("topo_in");
    in(x) = x;
    Approximation composed = Compose{TwoStep{"_inner"}, TwoStep{"_outer"}};
    EncodeResult ce = composed.encode({in});
    auto index_of = [&](const char *n) {
        for (size_t i = 0; i < ce.intermediates.size(); i++) {
            if (ce.intermediates[i].name() == n) {
                return (int)i;
            }
        }
        return -1;
    };
    // The Compose reports the inter-stage Func (the inner stage's step_b) and
    // both stages' step_a Funcs, but not its own output or input.
    if (ce.intermediates.size() != 3 || index_of("step_b_inner") < 0 || index_of("step_a_inner") < 0 ||
        index_of("step_a_outer") < 0) {
        printf("Compose intermediates wrong (%zu)\n", ce.intermediates.size());
        return 1;
    }
    // Each intermediate must appear after everything it calls.
    for (size_t i = 0; i < ce.intermediates.size(); i++) {
        for (const auto &[n, callee] : Internal::find_direct_calls(ce.intermediates[i].function())) {
            for (size_t j = i; j < ce.intermediates.size(); j++) {
                if (ce.intermediates[j].function().same_as(callee)) {
                    printf("Intermediates are not in topological order\n");
                    return 1;
                }
            }
        }
    }
    if (e.intermediates.size() != 1 || e.intermediates[0].name().rfind("step_a", 0) != 0) {
        printf("Unexpected TwoStep encode intermediates\n");
        return 1;
    }
    // Compose's stage_outputs: two children then the Compose itself.
    if (ce.stage_outputs.size() != 3 || ce.stage_outputs[2].intermediates.size() != 3 ||
        ce.stage_outputs[0].intermediates.size() != 1) {
        printf("Compose stage_outputs wrong\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (check_discovery()) {
        return 1;
    }

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
    // including repeated component types and Parallel/TrustedInverse ownership.
    Approximation first_identity = Identity{}, second_identity = Identity{};
    Approximation trusted_encoder = Identity{}, trusted_decoder = Identity{};
    Approximation applied = Parallel{std::vector<Approximation>{second_identity}};
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
        !require_port(applied, "Parallel") ||
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

    // encoded (q, d) come first, then amax, discovered without being declared.
    {
        std::vector<std::string> names;
        for (const Func &i : result.intermediates) {
            names.push_back(i.name());
        }
        if (names != std::vector<std::string>{"q", "d", "amax"}) {
            printf("Unexpected intermediates (%zu)\n", names.size());
            return 1;
        }
        // Stage-local intermediates are reported per stage, too.
        const ApproximationStageOutputs &enc_stage = result.encoded_stage_outputs.back();
        if (enc_stage.intermediates.size() != 1 || enc_stage.intermediates[0].name() != "amax") {
            printf("Unexpected per-stage intermediates\n");
            return 1;
        }
    }
    result.replacement.compute_root();
    for (Func intermediate : result.intermediates) {
        intermediate.compute_root();
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
        if (std::fabs(g_out(i) - expected_g) > 1e-5f * std::max(1.0f, std::fabs(expected_g))) {
            printf("g(%d) = %f, expected %f -- approximate_by's substitution did not take effect\n",
                   i, g_out(i), expected_g);
            return 1;
        }

        // h was never passed as a consumer to approximate_by(): it must
        // see the real f, not the quantized round trip.
        const float expected_h = fx * 3.0f;
        if (std::fabs(h_out(i) - expected_h) > 1e-5f * std::max(1.0f, std::fabs(expected_h))) {
            printf("h(%d) = %f, expected %f -- approximate_by affected a Func not in `consumers`\n",
                   i, h_out(i), expected_h);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}

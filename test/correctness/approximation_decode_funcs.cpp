#include "Halide.h"
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

using namespace Halide;

// ApproximationResult::decode_funcs() lists the decode chain that
// eager_inline() can fold into a consumer, so that schedule-time rewrites such
// as rfactor() see the encoded Funcs directly.

namespace {

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

// Symmetric per-block quantization of (within, block) to int8 codes and a scale.
struct BlockQuantizer {
    int block;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var i("i"), b("b");
        RDom r(0, block);
        Func amax("bq_amax"), scale("bq_scale"), codes("bq_codes");
        amax(b) = maximum(abs(in[0](r, b)));
        scale(b) = amax(b) / 127.0f;
        codes(i, b) = cast<int8_t>(round(in[0](i, b) / select(scale(b) == 0.0f, 1.0f, scale(b))));
        return {codes, scale};
    }

    std::vector<Func> decode(const std::vector<Func> &encoded) const {
        Var i("i"), b("b");
        Func out("bq_decoded");
        out(i, b) = cast<float>(encoded[0](i, b)) * encoded[1](b);
        return {out};
    }

    std::string name() const {
        return "BlockQuantizer";
    }
};

Approximation make_scheme() {
    Pointwise offset{"offset",
                     [](Expr x) { return cast<uint8_t>(cast<int>(x) + 128); },
                     [](Expr x) { return cast<int8_t>(cast<int>(x) - 128); }};
    Pointwise f16{"f16",
                  [](Expr x) { return cast<float16_t>(x); },
                  [](Expr x) { return cast<float>(x); }};
    return Compose(BlockReshape{8}, BlockQuantizer{8}, Parallel{offset, f16});
}

std::set<std::string> names_of(const std::vector<Func> &fs) {
    std::set<std::string> result;
    for (const Func &f : fs) {
        result.insert(f.name());
    }
    return result;
}

std::set<std::string> callees(const Func &f) {
    std::set<std::string> result;
    for (const auto &[name, callee] : Internal::find_direct_calls(f.function())) {
        result.insert(name);
    }
    return result;
}

constexpr int K = 64;

Expr weight(Expr k) {
    return sin(cast<float>(k) * 0.37f) * 3.0f;
}

Expr activation(Expr k) {
    return cos(cast<float>(k) * 0.11f);
}

// A dot product of approximated weights with activations; returns the result
// of realizing it, with `schedule` applied to (approx, dot, r) first.
template<typename Schedule>
float run_dot(const Schedule &schedule) {
    Var k("k");
    Func w("w"), a("a"), dot("dot");
    w(k) = weight(k);
    a(k) = activation(k);
    RDom r(0, K, "r");
    dot() = 0.0f;
    dot() += w(r) * a(r);
    ApproximationResult approx = w.approximate_by(make_scheme(), {dot});
    schedule(approx, dot, r);
    Buffer<float> out = dot.realize();
    return out();
}

int test_folds_decode_chain() {
    Var k("k");
    Func w("w"), a("a"), dot("dot");
    w(k) = weight(k);
    a(k) = activation(k);
    RDom r(0, K, "r");
    dot() = 0.0f;
    dot() += w(r) * a(r);
    ApproximationResult approx = w.approximate_by(make_scheme(), {dot});

    std::vector<Func> funcs = approx.decode_funcs();
    CHECK(!funcs.empty());
    CHECK(funcs.back().name() == approx.replacement.name());

    // Exactly the decode side: no encoded or encode-side Funcs.
    std::set<std::string> decode_side = names_of(approx.decode_trace.intermediates);
    decode_side.insert(approx.replacement.name());
    CHECK(names_of(funcs) == decode_side);
    for (const Func &e : approx.encoded) {
        CHECK(!names_of(funcs).count(e.name()));
    }

    // Dependency order: every callee in the list comes before its caller.
    std::set<std::string> earlier;
    std::set<std::string> listed = names_of(funcs);
    for (const Func &f : funcs) {
        for (const std::string &c : callees(f)) {
            CHECK(!listed.count(c) || earlier.count(c));
        }
        earlier.insert(f.name());
    }

    // One call folds the chain: the update now calls only the encoded Funcs,
    // the activations, and itself.
    dot.update().eager_inline(approx.decode_funcs());
    std::set<std::string> expected = names_of(approx.encoded);
    expected.insert(a.name());
    expected.insert(dot.name());
    CHECK(callees(dot) == expected);
    return 0;
}

int test_correct_with_rfactor() {
    float reference = run_dot([](const ApproximationResult &approx, Func dot, const RDom &) {
        for (Func f : approx.intermediates) {
            if (f.has_update_definition() || approx.is_stage_port(f)) {
                f.compute_root();
            }
        }
    });

    float folded = run_dot([](const ApproximationResult &approx, Func dot, const RDom &r) {
        for (const Func &e : approx.encoded) {
            Func(e).compute_root();
        }
        for (Func f : approx.intermediates) {
            if (f.has_update_definition()) {
                f.compute_root();
            }
        }
        dot.update().eager_inline(approx.decode_funcs());
        RVar ro("ro"), ri("ri");
        Var u("u");
        dot.update().split(r.x, ro, ri, 8);
        Func partial = dot.update().rfactor(ri, u);
        partial.compute_root().vectorize(u);
    });

    float exact = 0.0f;
    for (int k = 0; k < K; k++) {
        exact += std::sin(k * 0.37f) * 3.0f * std::cos(k * 0.11f);
    }
    if (std::abs(folded - reference) > 1e-3f || std::abs(folded - exact) > 0.5f) {
        printf("dot mismatch: folded %f, reference %f, exact %f\n", folded, reference, exact);
        return 1;
    }
    return 0;
}

int test_skips_scheduled_funcs() {
    Var k("k");
    Func w("w"), a("a"), dot("dot");
    w(k) = weight(k);
    a(k) = activation(k);
    RDom r(0, K, "r");
    dot() = 0.0f;
    dot() += w(r) * a(r);
    Approximation scheme = make_scheme();
    ApproximationResult approx = w.approximate_by(scheme, {dot});
    for (const Func &e : approx.encoded) {
        Func(e).compute_root();
    }
    for (Func f : approx.intermediates) {
        if (f.has_update_definition()) {
            f.compute_root();
        }
    }

    // Compute the dequantized blocks at root and vectorize the scale's decode:
    // both stay out of decode_funcs(), so eager_inline() does not reject it.
    Func dequantized = approx.decode_trace.children[1].ports[0];
    dequantized.compute_root();
    std::set<std::string> before = names_of(approx.decode_funcs());
    CHECK(before.count(dequantized.name()) == 0);

    std::vector<Func> decoded_scale_ports = approx.decode_trace.children[0].children[1].ports;
    CHECK(decoded_scale_ports.size() == 1);
    Func decoded_scale = decoded_scale_ports[0];
    CHECK(names_of(approx.decode_funcs()).count(decoded_scale.name()) == 1);
    decoded_scale.vectorize(decoded_scale.args()[0], 4);
    std::set<std::string> after = names_of(approx.decode_funcs());
    CHECK(after.count(decoded_scale.name()) == 0);
    CHECK(after.count(approx.replacement.name()) == 1);
    // A vectorized Func must be computed somewhere to lower.
    decoded_scale.compute_root();

    dot.update().eager_inline(approx.decode_funcs());
    // The scheduled Func is still called; everything inlinable is folded in.
    std::set<std::string> calls = callees(dot);
    CHECK(calls.count(dequantized.name()) == 1);
    CHECK(calls.count(approx.replacement.name()) == 0);

    Buffer<float> out = dot.realize();
    float exact = 0.0f;
    for (int i = 0; i < K; i++) {
        exact += std::sin(i * 0.37f) * 3.0f * std::cos(i * 0.11f);
    }
    if (std::abs(out() - exact) > 0.5f) {
        printf("scheduled dot mismatch: %f vs %f\n", out(), exact);
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (test_folds_decode_chain() || test_correct_with_rfactor() || test_skips_scheduled_funcs()) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}

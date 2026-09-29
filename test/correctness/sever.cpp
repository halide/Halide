#include "Halide.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace Halide;

// Exercises Pipeline::sever: rewriting calls to a Func into calls to
// a fresh ImageParam, severing a pipeline's *computation* of that Func while
// preserving the shape contract it stood in for.

namespace {

// Symmetric int8 quantization of a 1-D Func: the constructor defines codes q(k)
// and a scalar scale(); decode() dequantizes q(k) * scale().
struct SymmetricQuantize {
    Func q{"q"}, scale{"scale"}, amax{"amax"};

    explicit SymmetricQuantize(Func v, int k) {
        Var kv("k");
        RDom r(0, k, "r");

        amax() = 0.0f;
        amax() = max(amax(), abs(v(r)));

        scale() = amax() / 127.0f;

        Expr id = select(scale() != 0.0f, 1.0f / scale(), 0.0f);
        q(kv) = cast<int8_t>(clamp(round(v(kv) * id), -127, 127));
    }

    Func decode() const {
        Var kv("k");
        Func dequantized("dequantized");
        dequantized(kv) = cast<float>(q(kv)) * scale();
        return dequantized;
    }
};

void reference_symmetric_quantize(int k, const std::function<float(int)> &values,
                                  std::vector<int8_t> &q, float &scale) {
    float amax = 0.0f;
    for (int kk = 0; kk < k; kk++) {
        amax = std::max(amax, std::fabs(values(kk)));
    }
    scale = amax / 127.0f;
    float id = scale != 0.0f ? 1.0f / scale : 0.0f;
    q.resize(k);
    for (int kk = 0; kk < k; kk++) {
        int v = (int)std::round(values(kk) * id);
        q[kk] = (int8_t)std::max(-127, std::min(127, v));
    }
}

// A minimal check that sever() actually severs
// the call graph, rather than being a no-op that happens to still produce the
// right answer once. f(x) = x*2, g(x) = f(x) + 1: after
// Pipeline({g}).sever({f}), g must stop depending on f's own
// computation -- setting a buffer on the returned ImageParam that disagrees
// with f's true values must change g's output accordingly.
int minimal_severance_test() {
    Var x("x");
    Func f("f"), g("g");
    f(x) = x * 2;
    g(x) = f(x) + 1;

    SeverResult split = Pipeline({g}).sever({f});

    Buffer<int> f_values = split.offline.realize({10});
    for (int x = 0; x < 10; x++) {
        if (f_values(x) != x * 2) {
            printf("minimal_severance_test: offline f(%d) = %d, expected %d\n", x, f_values(x), x * 2);
            return 1;
        }
    }

    // Feed f's true values through the ImageParam: g should compute as if
    // nothing changed.
    split.online_inputs[0].set(f_values);
    Buffer<int> g_true = g.realize({10});
    for (int x = 0; x < 10; x++) {
        if (g_true(x) != x * 2 + 1) {
            printf("minimal_severance_test: g(%d) = %d with true f, expected %d\n", x, g_true(x), x * 2 + 1);
            return 1;
        }
    }

    // Now feed different values through the same ImageParam. If g still
    // depended on f's own computation, this would have no effect.
    Buffer<int> f_fake(10);
    for (int x = 0; x < 10; x++) {
        f_fake(x) = 1000 + x;
    }
    split.online_inputs[0].set(f_fake);
    Buffer<int> g_fake = g.realize({10});
    for (int x = 0; x < 10; x++) {
        int expected = f_fake(x) + 1;
        if (g_fake(x) != expected) {
            printf("minimal_severance_test: g(%d) = %d with fake f, expected %d "
                   "(sever() did not actually sever the call graph)\n",
                   x, g_fake(x), expected);
            return 1;
        }
    }

    return 0;
}

// A realistic case: sever a quantized vector's codes and scale from a
// consumer that dequantizes them, and check the final result still matches the
// plain-C++ reference round trip.
int quantized_offline_test() {
    const int K = 64;
    Var k("k");

    Func Vec("Vec");
    Vec(k) = cos(cast<float>(k) * 0.05f) * 3.0f;

    SymmetricQuantize quantize(Vec, K);

    Func Result("Result");
    Result(k) = quantize.decode()(k) * 2.0f;

    // q(k) and scale() are the Funcs Result's call graph depends on; amax() is
    // reachable only through them, so sever() severs it too.
    SeverResult split =
        Pipeline({Result}).sever({quantize.q, quantize.scale});

    // q(k) and scale() have different dimensionality (1-D vs scalar), so they
    // can't share a single realize({sizes}) call -- realize into
    // pre-allocated buffers of the right shape instead.
    Buffer<int8_t> q_buf(K);
    Buffer<float> scale_buf = Buffer<float>::make_scalar();
    split.offline.realize({q_buf, scale_buf});
    split.online_inputs[0].set(q_buf);
    split.online_inputs[1].set(scale_buf);

    Buffer<float> out = Result.realize({K});

    std::vector<int8_t> ref_q;
    float ref_scale;
    reference_symmetric_quantize(K, [](int kk) { return cosf(kk * 0.05f) * 3.0f; }, ref_q, ref_scale);
    for (int kk = 0; kk < K; kk++) {
        float expected = (ref_q[kk] * ref_scale) * 2.0f;
        if (std::fabs(out(kk) - expected) > 1e-3f * std::fabs(expected)) {
            printf("quantized_offline_test: Result(%d) = %f, expected %f\n", kk, out(kk), expected);
            return 1;
        }
    }

    return 0;
}

// The names overload mints ImageParams with caller-chosen names (and the
// severed Funcs' own types and dimensionalities), and they become the online
// pipeline's arguments.
int named_bindings_test() {
    const int K = 16;
    Var k("k");

    Func Vec("Vec");
    Vec(k) = cast<float>(k) - 7.5f;

    SymmetricQuantize quantize(Vec, K);

    Func Result("Result");
    Result(k) = quantize.decode()(k);

    SeverResult split =
        Pipeline({Result}).sever({quantize.q, quantize.scale}, {"q_in", "scale_in"});

    if (split.online_inputs.size() != 2 ||
        split.online_inputs[0].name() != "q_in" ||
        split.online_inputs[0].type() != Int(8) ||
        split.online_inputs[0].dimensions() != 1 ||
        split.online_inputs[1].name() != "scale_in" ||
        split.online_inputs[1].type() != Float(32) ||
        split.online_inputs[1].dimensions() != 0) {
        printf("named_bindings_test: online_inputs do not match the requested names/types\n");
        return 1;
    }

    std::vector<std::string> arg_names;
    for (const Argument &arg : Pipeline(Result).infer_arguments()) {
        arg_names.push_back(arg.name);
    }
    if (arg_names != std::vector<std::string>{"q_in", "scale_in"}) {
        printf("named_bindings_test: online pipeline's arguments are not {q_in, scale_in}\n");
        return 1;
    }

    Buffer<int8_t> q_buf(K);
    Buffer<float> scale_buf = Buffer<float>::make_scalar();
    split.offline.realize({q_buf, scale_buf});
    split.online_inputs[0].set(q_buf);
    split.online_inputs[1].set(scale_buf);
    Buffer<float> out = Result.realize({K});
    for (int kk = 0; kk < K; kk++) {
        float expected = q_buf(kk) * scale_buf();
        if (out(kk) != expected) {
            printf("named_bindings_test: Result(%d) = %f, expected %f\n", kk, out(kk), expected);
            return 1;
        }
    }

    return 0;
}

// The same quantizer expressed as an Approximation, used to check that
// compute_offline() composes with approximate_by()'s rewritten call graph.
struct ApproxSymmetricQuantize {
    explicit ApproxSymmetricQuantize(int k)
        : k_(k) {
    }

    EncodeResult encode(std::vector<Func> inputs) const {
        Func v = inputs[0];
        Var k("k");
        RDom r(0, k_, "r");

        Func amax("amax");
        amax() = 0.0f;
        amax() = max(amax(), abs(v(r)));

        Func d("scale");
        d() = amax() / 127.0f;

        Func q("q");
        Expr id = select(d() != 0.0f, 1.0f / d(), 0.0f);
        q(k) = cast<int8_t>(clamp(round(v(k) * id), -127, 127));

        return {{q, d}, {amax}};
    }

    DecodeResult decode(std::vector<Func> encoded) const {
        Func q = encoded[0], d = encoded[1];
        Var k("k");
        Func dequantized("dequantized");
        dequantized(k) = cast<float>(q(k)) * d();
        return {{dequantized}, {}};
    }

private:
    int k_;
};

// Sever a quantized vector's encode() outputs from a consumer built via
// approximate_by(), and check the final result still matches the plain-C++
// reference round trip.
int approximate_by_offline_test() {
    const int K = 64;
    Var k("k");

    Func Vec("Vec");
    Vec(k) = cos(cast<float>(k) * 0.05f) * 3.0f;

    ApproxSymmetricQuantize quantize(K);

    Func Result("Result");
    Result(k) = Vec(k) * 2.0f;

    ApproximationResult result = Vec.approximate_by(quantize, {Result});
    Result.eager_inline({result.replacement});

    // result.handles is [q, d, amax]: encode()'s two signature-contract
    // outputs, then its own scheduling-only handle. q and d are the actual
    // Funcs Result's call graph depends on (approximate_by() calls encode()
    // internally; a separately-called quantize.encode({Vec}) here would
    // build an unrelated, unconnected copy of the same graph shape).
    std::vector<Func> encoded = {result.handles[0], result.handles[1]};
    for (size_t i = 2; i < result.handles.size(); i++) {
        result.handles[i].compute_root();
    }

    ComputeOfflineResult split = Pipeline({Result}).compute_offline(encoded);

    Buffer<int8_t> q_buf(K);
    Buffer<float> scale_buf = Buffer<float>::make_scalar();
    split.offline.realize({q_buf, scale_buf});
    split.online_inputs[0].set(q_buf);
    split.online_inputs[1].set(scale_buf);

    Buffer<float> out = Result.realize({K});

    std::vector<int8_t> ref_q;
    float ref_scale;
    reference_symmetric_quantize(K, [](int kk) { return cosf(kk * 0.05f) * 3.0f; }, ref_q, ref_scale);
    for (int kk = 0; kk < K; kk++) {
        float expected = (ref_q[kk] * ref_scale) * 2.0f;
        if (std::fabs(out(kk) - expected) > 1e-3f * std::fabs(expected)) {
            printf("approximate_by_offline_test: Result(%d) = %f, expected %f\n", kk, out(kk), expected);
            return 1;
        }
    }

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (minimal_severance_test()) {
        return 1;
    }
    if (quantized_offline_test()) {
        return 1;
    }
    if (named_bindings_test()) {
        return 1;
    }
    if (approximate_by_offline_test()) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}

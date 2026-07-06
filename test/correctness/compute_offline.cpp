#include "Halide.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace Halide;

// Exercises Pipeline::compute_offline: rewriting calls to a Func into calls to
// a fresh ImageParam, severing a pipeline's *computation* of that Func while
// preserving the shape contract it stood in for.

namespace {

// Symmetric int8 quantization for a 1-D Func, same shape as the one in
// approximation_composition.cpp: encode() returns q(k) and scale(); decode()
// dequantizes q(k) * scale().
class SymmetricQuantize : public Approximation {
public:
    explicit SymmetricQuantize(int k)
        : k_(k) {
    }

    EncodeResult encode(std::vector<Func> inputs) override {
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

    DecodeResult decode(std::vector<Func> encoded) override {
        Func q = encoded[0], d = encoded[1];
        Var k("k");
        Func dequantized("dequantized");
        dequantized(k) = cast<float>(q(k)) * d();
        return {{dequantized}, {}};
    }

private:
    int k_;
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

// A minimal, non-Approximation check that compute_offline() actually severs
// the call graph, rather than being a no-op that happens to still produce the
// right answer once. f(x) = x*2, g(x) = f(x) + 1: after
// Pipeline({g}).compute_offline({f}), g must stop depending on f's own
// computation -- setting a buffer on the returned ImageParam that disagrees
// with f's true values must change g's output accordingly.
int minimal_severance_test() {
    Var x("x");
    Func f("f"), g("g");
    f(x) = x * 2;
    g(x) = f(x) + 1;

    ComputeOfflineResult split = Pipeline({g}).compute_offline({f});

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
                   "(compute_offline() did not actually sever the call graph)\n",
                   x, g_fake(x), expected);
            return 1;
        }
    }

    return 0;
}

// A realistic case: sever a quantized vector's encode() output from a
// consumer built via approximate_by(), and check the final result still
// matches the plain-C++ reference round trip.
int quantized_offline_test() {
    const int K = 64;
    Var k("k");

    Func Vec("Vec");
    Vec(k) = cos(cast<float>(k) * 0.05f) * 3.0f;

    SymmetricQuantize quantize(K);

    Func Result("Result");
    Result(k) = Vec(k) * 2.0f;

    ApproximationResult result = Vec.approximate_by(quantize, {Result});
    Result.inline_calls({result.replacement});

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

}  // namespace

int main(int argc, char **argv) {
    if (minimal_severance_test()) {
        return 1;
    }
    if (quantized_offline_test()) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}

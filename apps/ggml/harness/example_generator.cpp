#include "Halide.h"

using namespace Halide;

namespace {

// Plumbing example, not a tuned kernel: q8_0 weights x f32 activations in
// the kernel ABI of harness/halide_providers.cpp.
class Example : public Generator<Example> {
public:
    Input<Buffer<uint8_t, 2>> w{"w"};     // [row bytes, N], ggml block_q8_0 rows
    Input<Buffer<float, 2>> a{"a"};       // [K, M]
    Output<Buffer<float, 2>> out{"out"};  // [N, M]

    void generate() {
        Var n("n"), m("m");
        RDom r(0, a.dim(0).extent() / 32, 0, 32);
        Expr blk = r.x * 34;
        Expr d = cast<float>(reinterpret<float16_t>(cast<uint16_t>(w(blk, n)) | (cast<uint16_t>(w(blk + 1, n)) << 8)));
        Expr q = cast<float>(reinterpret<int8_t>(w(blk + 2 + r.y, n)));
        out(n, m) = 0.0f;
        out(n, m) += d * q * a(r.x * 32 + r.y, m);
        out.update().parallel(n, 16, TailStrategy::GuardWithIf);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Example, example)

// Fills a buffer with uniform random floats from xoshiro256++, the
// generator family behind Julia's default RNG: 256 bits of state per
// stream advance every step, and each step's output is two floats built
// from the halves of its 64-bit result - the same use-all-the-bits bulk
// fill contract Julia's Random.XoshiroSimd runs. The state is large,
// evolving, and never wanted by the consumer - only the projection is.
//
// Written two ways. As an inductive Func the state walks the stream in a
// two-deep folded window that never leaves registers, and only the floats
// reach memory. As an update definition over an RDom the walk owns its
// axis, so the full 32-byte-per-step state trajectory must be materialized
// before the projection can read it: eight times the output's traffic in
// state alone, for values nothing wants.

#include "Halide.h"

using namespace Halide;

namespace {

class Rng : public Halide::Generator<Rng> {
public:
    GeneratorParam<int> lanes{"lanes", 32};
    enum class ScanForm { Inductive,
                          Unfolded,
                          RDom };
    GeneratorParam<ScanForm> scan{"scan",
                                  ScanForm::Inductive,
                                  {{"inductive", ScanForm::Inductive},
                                   {"unfolded", ScanForm::Unfolded},
                                   {"rdom", ScanForm::RDom}}};
    GeneratorParam<bool> par{"par", false};

    // Four words of seed per stream.
    Input<Buffer<uint64_t, 2>> seeds{"seeds"};
    // Stream by step.
    Output<Buffer<float, 2>> y{"y"};

    void generate() {
        Var l("l"), t("t");

        auto rotl = [](Expr x, int k) {
            return (x << k) | (x >> (64 - k));
        };
        // One xoshiro256++ advance, given the four current words.
        auto step = [&](Expr s0, Expr s1, Expr s2, Expr s3) {
            Expr t17 = s1 << 17;
            Expr n2 = s2 ^ s0;
            Expr n3 = s3 ^ s1;
            Expr n1 = s1 ^ n2;
            Expr n0 = s0 ^ n3;
            return std::vector<Expr>{n0, n1, n2 ^ t17, rotl(n3, 45)};
        };
        // The two outputs a step yields: each half of the 64-bit result
        // becomes a float in [0, 1) by stuffing its top 23 bits into the
        // mantissa of a float in [1, 2) and shifting down. Integer ops
        // only, and both halves are the same shift of a 32-bit word.
        auto out = [&](Expr s0, Expr s3, Expr half) {
            Expr r = rotl(s0 + s3, 23) + s0;
            Expr w = select(half == 0, cast<uint32_t>(r),
                            cast<uint32_t>(r >> 32));
            Expr bits = (w >> 9) | Expr((uint32_t)0x3f800000);
            return reinterpret<float>(bits) - 1.f;
        };

        Func S(std::vector<Type>(4, UInt(64)), "S");
        RDom rt(1, y.dim(1).extent() - 1, "rt");
        if (scan != ScanForm::RDom) {
            Expr s0 = S(l, t - 1)[0], s1 = S(l, t - 1)[1];
            Expr s2 = S(l, t - 1)[2], s3 = S(l, t - 1)[3];
            auto n = step(s0, s1, s2, s3);
            std::vector<Expr> defs;
            for (int i = 0; i < 4; i++) {
                defs.push_back(select(t <= 0, seeds(i, l), likely(n[i])));
            }
            S(l, t) = Tuple(defs);
        } else {
            S(l, t) = Tuple(seeds(0, l), seeds(1, l), seeds(2, l), seeds(3, l));
            auto n = step(S(l, rt - 1)[0], S(l, rt - 1)[1],
                          S(l, rt - 1)[2], S(l, rt - 1)[3]);
            S(l, rt) = Tuple(n);
        }
        // Output lane pairs (2l, 2l+1) carry the low and high halves of
        // stream l's step.
        y(l, t) = out(S(l / 2, t)[0], S(l / 2, t)[3], l % 2);

        // ---------------- Schedule ----------------

        const int VEC = 8;  // 64-bit lanes of one vector
        Var co("co"), ci("ci");
        if (scan != ScanForm::RDom) {
            // Blocks of streams advance together through one serial walk;
            // their chains are independent, which keeps every port busy.
            y.split(l, co, ci, 2 * VEC)
                .reorder(ci, co, t)
                .vectorize(ci);
            if (par) {
                Var coo("coo"), coi("coi");
                y.split(co, coo, coi, 2)
                    .reorder(ci, coi, t, coo)
                    .unroll(coi)
                    .parallel(coo);
                S.store_at(y, coo)
                    .compute_at(y, coi)
                    .vectorize(l, VEC);
            } else {
                y.unroll(co);
                S.store_root()
                    .compute_at(y, co)
                    .vectorize(l, VEC);
            }
            if (scan == ScanForm::Inductive) {
                S.fold_storage(t, 2);
            }
        } else {
            // The materialized walk, at its best: everything nested inside
            // the output's stream-block loop as one parallel loop.
            y.split(l, co, ci, 2 * VEC)
                .reorder(ci, t, co)
                .vectorize(ci);
            if (par) {
                y.parallel(co);
            }
            S.compute_at(y, co)
                .vectorize(l, VEC);
            S.update()
                .reorder(l, rt)
                .vectorize(l, VEC);
        }

        seeds.dim(0).set_bounds(0, 4);
        seeds.dim(1).set_bounds(0, lanes);
        y.dim(0).set_bounds(0, 2 * (int)lanes);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Rng, rng)

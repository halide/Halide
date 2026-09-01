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
        // The step's 64-bit result, computed at the state's own lane
        // width - a Func of its own, so the arithmetic happens before the
        // lanes are duplicated into output pairs.
        Func r64("r64");
        r64(l, t) = rotl(S(l, t)[0] + S(l, t)[3], 23) + S(l, t)[0];

        // Output lane pairs (2l, 2l+1) carry the low and high halves of
        // stream l's word; extract_bits at the interleaving position is a
        // free vector reinterpret. Each half's top 24 bits convert and
        // scale to a float in [0, 1) - bit-exact with what Julia's
        // Random.XoshiroSimd bulk fill produces.
        Func r32("r32");
        r32(l, t) = extract_bits<uint32_t>(r64(l / 2, t), 32 * (l % 2));
        y(l, t) = cast<float>(r32(l, t) >> 8) * (1.f / 16777216.f);

        // ---------------- Schedule ----------------

        const int VEC = lanes;  // 64-bit lanes of one vector
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
                    .vectorize(l);
                r64.compute_at(y, coi).vectorize(l);
                r32.compute_at(y, coi).vectorize(l);
            } else {
                y.unroll(co);
                S.store_root()
                    .compute_at(y, co)
                    .vectorize(l);
                r64.compute_at(y, co).vectorize(l);
                r32.compute_at(y, co).vectorize(l);
            }
            if (scan == ScanForm::Inductive) {
                Var to("to"), ti("ti");
                S.slide(y, t).fold_storage(t, 2);
                y.split(t, to, ti, 2, TailStrategy::RoundUp).unroll(ti);
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
                .vectorize(l);
            S.update()
                .reorder(l, rt)
                .vectorize(l);
            r64.compute_at(y, co).vectorize(l);
            r32.compute_at(y, co).vectorize(l);
        }

        seeds.dim(0).set_bounds(0, 4);
        seeds.dim(1).set_bounds(0, lanes);
        y.dim(0).set_bounds(0, 2 * (int)lanes);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Rng, rng)

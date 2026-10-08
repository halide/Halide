#include "Halide.h"
#include <sstream>
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

// Register blocking with split_storage: f is split in storage by the same
// factors that its stages are split and vectorized by, so each vectorized tile
// of f is the dense innermost block of its storage. Every vector access to f
// should then be a dense ramp, which lets LLVM keep f in registers. The tiles
// exactly cover f, so the tail strategy's clamp (and blend) should simplify
// away rather than hide the alignment of the tiles.

namespace {

class HasSelect : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Select *op) override {
        result = true;
    }

public:
    bool result = false;
};

class CheckDense : public IRVisitor {
    using IRVisitor::visit;

    void check(const std::string &name, const Expr &index, const char *kind) {
        if (name != func || index.type().is_scalar()) {
            return;
        }
        const Ramp *r = index.as<Ramp>();
        if (r && is_const_one(r->stride)) {
            dense++;
        } else {
            printf("  non-dense %s of %s: %s\n", kind, func.c_str(), index_string(index).c_str());
            other++;
        }
    }

    static std::string index_string(const Expr &e) {
        std::ostringstream s;
        s << e;
        return s.str();
    }

    void visit(const Store *op) override {
        check(op->name, op->index, "store");
        if (op->name == func) {
            // A blend that survives shows up as a select or a predicate.
            HasSelect s;
            op->value.accept(&s);
            if (s.result || !is_const_one(op->predicate)) {
                printf("  blended store of %s: %s\n", func.c_str(), index_string(op->value).c_str());
                other++;
            }
        }
        IRVisitor::visit(op);
    }

    void visit(const Load *op) override {
        check(op->name, op->index, "load");
        IRVisitor::visit(op);
    }

public:
    CheckDense(const std::string &func)
        : func(func) {
    }

    std::string func;
    int dense = 0, other = 0;
};

enum class Mode {
    ShiftInwards,
    ShiftInwardsAndBlend,
    AlignedShiftInwards,
    AlignedShiftInwardsAndBlend,
};

const char *mode_name(Mode m) {
    switch (m) {
    case Mode::ShiftInwards:
        return "ShiftInwards";
    case Mode::ShiftInwardsAndBlend:
        return "ShiftInwardsAndBlend";
    case Mode::AlignedShiftInwards:
        return "aligned ShiftInwards";
    case Mode::AlignedShiftInwardsAndBlend:
        return "aligned ShiftInwardsAndBlend";
    }
    return "";
}

bool test(int vx, int vy, bool unroll, Mode mode) {
    const int K = 16, tx = 2 * vx, ty = 2 * vy;
    const int N = 4 * tx, M = 3 * ty;

    ImageParam in(Int(32), 2, "in");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");
    Var xio("xio"), xii("xii"), yio("yio"), yii("yii");
    Var sxo("sxo"), sxi("sxi"), syo("syo"), syi("syi");
    RDom r(0, K, "r");
    Func f("f"), g("g");
    f(x, y) = x + 2 * y;
    f(x, y) += in(r, x) * in(r, y);
    g(x, y) = f(x, y);

    g.tile(x, y, xo, yo, xi, yi, tx, ty, TailStrategy::RoundUp);
    f.compute_at(g, xo);
    f.split_storage(x, sxo, sxi, vx)
        .split_storage(y, syo, syi, vy)
        .reorder_storage(sxi, syi, sxo, syo);
    bool blend = (mode == Mode::ShiftInwardsAndBlend ||
                  mode == Mode::AlignedShiftInwardsAndBlend);
    bool aligned = (mode == Mode::AlignedShiftInwards ||
                    mode == Mode::AlignedShiftInwardsAndBlend);
    // The pure stage uses the tail strategy under test. The update is a
    // reduction, so it can only use the blend variant (or RoundUp).
    TailStrategy pure_tail = blend ? TailStrategy::ShiftInwardsAndBlend : TailStrategy::ShiftInwards;
    TailStrategy update_tail = blend ? TailStrategy::ShiftInwardsAndBlend : TailStrategy::RoundUp;
    // Anchor aligned splits at the output's min, which the tiles of g (and so
    // the realizations of f) are anchored to.
    Expr align_x = g.output_buffer().dim(0).min();
    Expr align_y = g.output_buffer().dim(1).min();
    for (int stage = 0; stage < 2; stage++) {
        Stage s = stage == 0 ? Stage(f) : f.update();
        TailStrategy tail = stage == 0 ? pure_tail : update_tail;
        if (aligned) {
            s.aligned_split(x, xio, xii, vx, align_x, tail)
                .aligned_split(y, yio, yii, vy, align_y, tail);
        } else {
            s.split(x, xio, xii, vx, tail)
                .split(y, yio, yii, vy, tail);
        }
        s.reorder(xii, yii, xio, yio)
            .vectorize(xii)
            .vectorize(yii);
        if (unroll) {
            s.unroll(xio).unroll(yio);
        }
    }

    CheckDense c(f.name());
    Module m = g.compile_to_module({in}, "g", get_jit_target_from_environment());
    for (const LoweredFunc &lf : m.functions()) {
        lf.body.accept(&c);
    }
    if (c.other || !c.dense) {
        printf("%dx%d tile, %s%s: %d dense and %d non-dense or blended vector accesses of f\n",
               vx, vy, mode_name(mode), unroll ? ", unrolled" : "", c.dense, c.other);
        return false;
    }

    Buffer<int> in_buf(K, std::max(N, M));
    in_buf.for_each_element([&](int k, int i) { in_buf(k, i) = (k * 7 + i * 3) % 11 - 5; });
    in.set(in_buf);
    Buffer<int> out = g.realize({N, M});
    for (int j = 0; j < M; j++) {
        for (int i = 0; i < N; i++) {
            int correct = i + 2 * j;
            for (int k = 0; k < K; k++) {
                correct += in_buf(k, i) * in_buf(k, j);
            }
            if (out(i, j) != correct) {
                printf("%dx%d tile, %s%s: out(%d, %d) = %d instead of %d\n",
                       vx, vy, mode_name(mode), unroll ? ", unrolled" : "",
                       i, j, out(i, j), correct);
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    bool ok = true;
    for (Mode mode : {Mode::ShiftInwards, Mode::ShiftInwardsAndBlend,
                      Mode::AlignedShiftInwards, Mode::AlignedShiftInwardsAndBlend}) {
        for (bool unroll : {true, false}) {
            // Aligned outer loops start at a symbolic min, so the simplifier
            // can only resolve their clamps once they're unrolled.
            if (!unroll && (mode == Mode::AlignedShiftInwards ||
                            mode == Mode::AlignedShiftInwardsAndBlend)) {
                continue;
            }
            for (auto [vx, vy] : {std::pair{2, 2}, {2, 4}, {4, 2}}) {
                ok = test(vx, vy, unroll, mode) && ok;
            }
        }
    }
    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}

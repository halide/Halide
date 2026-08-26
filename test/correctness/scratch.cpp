// A panel staged once per step of a slid walk is allocated the whole prefix of
// the walk rather than the one step it is read at, when there is a loop between
// where the panel is computed and where the slid Func is.
//
// The shape is the one flash attention has. A consumer walks a reduction
// dimension in steps. Carried across the walk is a Func defined inductively
// over that dimension - what it was one step ago combined with this step -
// which slides over the walk and folds down to the two steps that are live.
// Feeding it is a panel, staged once per step and the same for every row, so
// the panel wants to be computed outside the loop over rows while everything
// that depends on a row is computed inside it.
//
// The panel is read at step t and nowhere else, so one step of it is live and
// its allocation should be one step's worth. With the panel and the slid Func
// at the same loop level that is what happens. Put a loop between them - which
// is what sharing the panel between rows requires - and the panel's extent
// becomes the whole walk so far, written in terms of the step.
//
// On a GPU schedule the loop in between is the loop over warps, and the symptom
// is a loop whose extent is not a constant, so it cannot be unrolled.

#include "Halide.h"
#include <cmath>
#include <sstream>
#include <stdio.h>
#include <string>
#include <vector>

using namespace Halide;

// The size of a Func's allocation, and the extents it was built from.
class AllocationSizeOf : public Internal::IRMutator {
    const std::string producer;
    using Internal::IRMutator::visit;
    Internal::Stmt visit(const Internal::Allocate *op) override {
        if (op->name == producer) {
            int64_t total = 1;
            extents.clear();
            for (const auto &e : op->extents) {
                total *= Internal::as_const_int(e).value_or(0);
                std::ostringstream s;
                s << e;
                extents += (extents.empty() ? "" : " * ") + s.str();
            }
            size = (int)total;
        }
        return Internal::IRMutator::visit(op);
    }

public:
    int size = 0;
    std::string extents;
    AllocationSizeOf(std::string producer)
        : producer(std::move(producer)) {
    }
};

// The width of one step's panel, how many rows the walk carries state for, how
// many of those a group takes, and how many steps the walk takes. Neither
// splitting the walk nor folding the carried Func's storage is needed to see
// this; the slide and the loop in between are enough.
const int W = 8, Y = 4, R = 2, N = 16;

// Whether the panel is computed outside the loop over groups of rows, which is
// what sharing it between them requires, or alongside everything else.
int run(bool panel_outside_row_loop, int *panel_size, std::string *panel_extents) {
    Var x("x"), y("y"), t("t"), yo("yo"), yw("yw"), yi("yi");
    RVar rto("rto"), rti("rti");
    RDom rt(0, N, "rt");
    RDom rx(0, W, "rx");

    // Staged once per step, and the same for every row.
    Func panel("panel");
    panel(x, t) = cast<float>(x + t);

    // What this step contributes to a row.
    Func step("step");
    step(y, t) = 0.f;
    step(y, t) += panel(rx, t) + y;

    // Carried across the walk: what it was one step ago, combined with this
    // step. Asked for the step before the first, it is just that step.
    Func m(Float(32), "m");
    m(y, t) = select(t <= 0, step(y, t),
                     likely(max(m(y, t - 1), step(y, t))));

    // The walk. Each step rescales what it has by how far the carried value
    // moved, then adds this step's panel.
    Func acc("acc");
    acc(x, y) = 0.f;
    acc(x, y) = acc(x, y) * (m(y, rt) - m(y, rt - 1)) + panel(x, rt);

    Func out("out");
    out(x, y) = acc(x, y);

    out.bound(x, 0, W).bound(y, 0, Y).split(y, yo, yi, Y).compute_root();

    acc.compute_at(out, yo);
    // A loop over groups of rows inside the walk.
    acc.update()
        .split(y, yw, yi, R)
        .reorder(x, yi, yw, rt);

    // Everything that depends on a row is computed per group of rows.
    step.compute_at(acc, yw);
    m.store_at(out, yo)
        .compute_at(acc, yw)
        .slide(acc, rt)
        ;

    // The panel does not depend on a row. Sharing it between the groups means
    // computing it outside the loop over them.
    if (panel_outside_row_loop) {
        panel.compute_at(acc, rt);
    } else {
        panel.compute_at(acc, yw);
    }

    AllocationSizeOf alloc(panel.name());
    out.add_custom_lowering_pass(&alloc, nullptr);
    Buffer<float> result = out.realize({W, Y});

    // The answer does not depend on the schedule, so check it against the same
    // walk done here.
    auto step_of = [&](int yy, int tt) {
        float s = 0;
        for (int j = 0; j < W; j++) {
            s += (float)(j + tt) + yy;
        }
        return s;
    };
    for (int yy = 0; yy < Y; yy++) {
        std::vector<float> mv(N);
        for (int i = 0; i < N; i++) {
            mv[i] = (i <= 0) ? step_of(yy, i) : std::max(mv[i - 1], step_of(yy, i));
        }
        const float m_before = step_of(yy, -1);
        for (int j = 0; j < W; j++) {
            float a = 0;
            for (int i = 0; i < N; i++) {
                a = a * (mv[i] - (i > 0 ? mv[i - 1] : m_before)) + (float)(j + i);
            }
            if (std::abs(result(j, yy) - a) > 1e-3f * std::abs(a) + 1e-4f) {
                printf("  wrong value at %d %d: %f != %f\n", j, yy,
                       (double)result(j, yy), a);
                return -1;
            }
        }
    }
    *panel_size = alloc.size;
    *panel_extents = alloc.extents;
    return 0;
}

int main(int argc, char **argv) {
    bool ok = true;
    for (bool outside : {false, true}) {
        int size = 0;
        std::string extents;
        if (run(outside, &size, &extents) != 0) {
            return 1;
        }
        printf("panel computed %-22s: allocation %3d  [%s]\n",
               outside ? "outside the row loop" : "with everything else", size,
               extents.c_str());
        ok &= (size == W);
    }
    printf("\nThe panel is read at one step of the walk and nowhere else, so "
           "both want to be %d.\n",
           W);
    return ok ? 0 : 1;
}

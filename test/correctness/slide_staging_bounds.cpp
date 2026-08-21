#include "Halide.h"
#include <stdio.h>
#include <string>
#include <vector>

using namespace Halide;

// Bounds inference emits a .min/.max let per enclosing loop level, and sliding
// rewrites each one with a bound valid where it sits. A consumer computed
// outside the slid Func's own loop reads an outer one, so if that one still
// asks for the region required before the window slid, the consumer is sized
// for it - the whole walk, rather than the sliver that is live.
//
// A consumer walks a reduction dimension in steps, carrying a Func that slides
// along it. Feeding that Func is a panel, staged once per step and the same
// for every row, so the panel is computed outside the loop over rows while
// everything that depends on a row is computed inside it. That loop is what
// puts the panel and the slid Func at different levels.
//
// The panel is read at one step and nowhere else, so its allocation should be
// one step's worth however it is scheduled.

// Reports the size of a producer's allocation, or 0 if it isn't constant.
class AllocationSizeOf : public Internal::IRMutator {
    const std::string producer;

    using IRMutator::visit;

    Internal::Stmt visit(const Internal::Allocate *op) override {
        if (op->name == producer) {
            int64_t total = 1;
            for (const auto &e : op->extents) {
                total *= Internal::as_const_int(e).value_or(0);
            }
            size = (int)total;
        }
        return IRMutator::visit(op);
    }

public:
    int size = 0;

    AllocationSizeOf(std::string producer)
        : producer(std::move(producer)) {
    }
};

// The width of one step's panel, how many rows the walk carries state for, how
// many of those a group takes, and how many steps the walk takes.
const int W = 8, Y = 4, R = 2, N = 16;

// Whether the panel is computed outside the loop over groups of rows, which is
// what sharing it between them requires, or alongside everything else.
int run(bool panel_outside_row_loop, int *panel_size) {
    Var x("x"), y("y"), t("t"), yo("yo"), yw("yw"), yi("yi");
    RDom rt(0, N, "rt");
    RDom rx(0, W, "rx");

    // Staged once per step, and the same for every row.
    Func panel("panel");
    panel(x, t) = cast<float>(x + t);

    // What this step contributes to a row.
    Func step("step");
    step(y, t) = 0.f;
    step(y, t) += panel(rx, t) + y;

    // The walk reads two consecutive steps of this, so it slides along the
    // walk and only the newest sliver is computed each time.
    Func acc("acc");
    acc(x, y) = 0.f;
    acc(x, y) = acc(x, y) * (step(y, rt) - step(y, rt - 1)) + panel(x, rt);

    Func out("out");
    out(x, y) = acc(x, y);

    out.bound(x, 0, W).bound(y, 0, Y).split(y, yo, yi, Y).compute_root();

    acc.compute_at(out, yo);
    // A loop over groups of rows inside the walk.
    acc.update()
        .split(y, yw, yi, R)
        .reorder(x, yi, yw, rt);

    // Everything that depends on a row is computed per group of rows. Stored
    // outside the walk and computed within it, so it slides.
    step.store_at(out, yo).compute_at(acc, yw);

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
        for (int j = 0; j < W; j++) {
            float a = 0;
            for (int i = 0; i < N; i++) {
                a = a * (step_of(yy, i) - step_of(yy, i - 1)) + (float)(j + i);
            }
            if (result(j, yy) != a) {
                printf("Wrong value at %d %d: %f != %f\n", j, yy,
                       (double)result(j, yy), a);
                return -1;
            }
        }
    }
    *panel_size = alloc.size;
    return 0;
}

int main(int argc, char **argv) {
    for (bool outside : {false, true}) {
        int size = 0;
        if (run(outside, &size) != 0) {
            return 1;
        }
        if (size != W) {
            printf("The panel is read at one step of the walk and nowhere "
                   "else, so with it computed %s it should be allocated %d "
                   "elements, but it was allocated %s.\n",
                   outside ? "outside the row loop" : "with everything else", W,
                   size ? std::to_string(size).c_str() : "an extent that isn't constant");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}

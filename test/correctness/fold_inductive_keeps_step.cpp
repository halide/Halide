// A Func defined in terms of its own previous value reads that value while
// computing the next one, and never again, so one slot would do if every
// point were computed exactly once. A redundant split or a ShiftInwards tail
// computes points twice, and then the point's own store would overwrite the
// step it reads. Nothing checks for that, so the window always keeps the
// step: two slots for a reach of one, whether the coordinate the producer
// writes is plain or tiled (a tiled coordinate, yo*4 + yi, is what a real
// schedule looks like, and is where the arithmetic tends to go wrong).
//
// The values come out right either way - only the size of the allocation is at
// stake - so this asserts on the allocation rather than on the answer.

#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Records the extent of the allocation for one Func.
class RecordAllocationExtent : public IRMutator {
    using IRMutator::visit;
    Stmt visit(const Allocate *op) override {
        if (op->name == func) {
            int64_t e = 1;
            for (const Expr &s : op->extents) {
                if (auto c = as_const_int(s)) {
                    e *= *c;
                } else {
                    e = -1;
                    break;
                }
            }
            *extent = e;
        }
        return IRMutator::visit(op);
    }

public:
    std::string func;
    int64_t *extent;
    RecordAllocationExtent(std::string f, int64_t *e)
        : func(std::move(f)), extent(e) {
    }
};

// The width of one value of the slid dimension, so the extent can be read as a
// number of slots.
const int W = 8, N = 16;

// consume_previous chooses whether the consumer also asks for the older value.
int64_t allocation_slots(bool tiled) {
    Var y("y"), t("t"), yo("yo"), yi("yi"), to("to"), ti("ti");
    Func step("step");
    step(y, t) = cast<float>(y + t);

    std::string mname = tiled ? "m_tiled" : "m_plain";
    Func m = Func(Float(32), mname);
    m(y, t) = select(t <= 0,
                     step(y, t),
                     likely(m(y, t - 1) + step(y, t)));

    Func out("out");
    out(y, t) = m(y, t);

    out.bound(y, 0, W).bound(t, 0, N);
    // Split the consumer's loop by one, so that the loop the producer is
    // computed in runs once and there is nothing for the loop-based sliding to
    // do with it.
    out.split(t, to, ti, 1);
    m.compute_at(out, ti).store_root().slide(out, t);
    if (tiled) {
        // The coordinate the producer writes becomes yo*4 + yi.
        m.split(y, yo, yi, 4);
    }

    int64_t extent = -1;
    out.add_custom_lowering_pass(new RecordAllocationExtent(mname, &extent));
    out.compile_jit(get_host_target());

    // The answers have to be right whatever the allocation is.
    Buffer<float> result = out.realize({W, N});
    for (int t = 0; t < N; t++) {
        for (int y = 0; y < W; y++) {
            float want = 0;
            for (int i = 0; i <= t; i++) {
                want += (float)(y + i);
            }
            if (result(y, t) != want) {
                printf("out(%d, %d) = %f instead of %f\n", y, t, result(y, t), want);
                return -2;
            }
        }
    }
    return extent < 0 ? extent : extent / W;
}

}  // namespace

int main(int argc, char **argv) {
    int64_t plain = allocation_slots(false);
    int64_t tiled = allocation_slots(true);
    printf("slots when the producer writes a plain coordinate: %d\n", (int)plain);
    printf("slots when it writes a tiled one:                  %d\n", (int)tiled);

    int failures = 0;
    if (plain != 2) {
        printf("FAIL: the window should keep the step the recurrence reads, two "
               "slots, but %d were allocated\n", (int)plain);
        failures++;
    }
    if (tiled != 2) {
        printf("FAIL: tiling the coordinate the producer writes should not change "
               "the window, two slots, but %d were allocated\n", (int)tiled);
        failures++;
    }
    if (failures) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}

// A Func defined in terms of its own previous value reads that value while
// computing the next one, and never again. The read dies at the store that
// replaces it, so one slot is enough to slide over the dimension.
//
// Whether that is noticed depends on being able to say that two iterations of
// the loops inside the producer never write the same place. Asking a solver to
// falsify a collision does not survive a split: that 16a + b == 16a' + b' with
// b and b' in [0, 15] forces a == a' is a uniqueness-of-representation
// argument, and a tiled schedule writes coordinates of exactly that shape. So
// the case below tiles the dimensions the producer writes.
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
    if (plain != 1) {
        printf("FAIL: the older value dies at the store that replaces it, so one "
               "slot is enough, but %d were allocated\n", (int)plain);
        failures++;
    }
    if (tiled != 1) {
        printf("FAIL: tiling the coordinate the producer writes does not change "
               "when the older value dies, so one slot is still enough, but %d "
               "were allocated\n", (int)tiled);
        failures++;
    }
    if (failures) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}

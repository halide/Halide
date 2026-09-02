#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// The explicit slide directive with an explicit one-slot fold: the
// schedule asserts the in-place form (each point computed once), and the
// window must honour it rather than widening to keep the previous step.
int main(int argc, char **argv) {
    Func f(Int(32), 1, "f"), g("g");
    Var t("t"), to("to"), ti("ti");
    f(t) = select(t <= 0, 1, likely(f(t - 1) * 3 % 1000 + 1));
    g(t) = f(t) + 0;

    g.split(t, to, ti, 4, TailStrategy::RoundUp).unroll(ti);
    f.store_root().compute_at(g, ti).slide(g, t).fold_storage(t, 1);

    // The one-slot allocation is the point: check it in the lowered code.
    class CountSlots : public Internal::IRMutator {
        using IRMutator::visit;
        Internal::Stmt visit(const Internal::Allocate *op) override {
            if (op->name == "f") {
                slots = op->extents[0];
            }
            return IRMutator::visit(op);
        }

    public:
        Expr slots;
    } counter;
    g.add_custom_lowering_pass(&counter, nullptr);

    Buffer<int> out = g.realize({64});
    if (!Internal::is_const(counter.slots, 1)) {
        printf("f was allocated %s slots instead of 1\n",
               counter.slots.defined() ? "more" : "an unknown number of");
        return 1;
    }
    int v = 1;
    for (int t = 0; t < 64; t++) {
        if (t > 0) v = v * 3 % 1000 + 1;
        if (out(t) != v) {
            printf("out(%d) = %d instead of %d\n", t, out(t), v);
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}

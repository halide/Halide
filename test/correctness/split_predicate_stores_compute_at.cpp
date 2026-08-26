#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Note: this test is built with NDEBUG, so assert() compiles to nothing.
bool check(bool ok, const char *msg) {
    if (!ok) {
        printf("Failed: %s\n", msg);
    }
    return ok;
}

// Counts IfThenElse nodes reached while inside the named Func's produce node.
class CountIfsInProduce : public IRVisitor {
    using IRVisitor::visit;

    std::string name;
    int depth = 0;

    void visit(const ProducerConsumer *op) override {
        if (op->is_producer && op->name == name) {
            depth++;
            IRVisitor::visit(op);
            depth--;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const IfThenElse *op) override {
        if (depth > 0) {
            count++;
        }
        IRVisitor::visit(op);
    }

public:
    explicit CountIfsInProduce(std::string n)
        : name(std::move(n)) {
    }
    int count = 0;
};

}  // namespace

int main(int argc, char **argv) {
    // A producer compute_at a plain (non-aligned) split tile of its
    // consumer, with the consumer's tail handled by PredicateStores rather
    // than GuardWithIf. PredicateStores only predicates the consumer's
    // store, not the loads that feed it, so the producer's required region
    // for a boundary tile still comes out tied to the consumer's declared
    // extent rather than as an unconditional full tile -- bounds inference
    // needs to find a compile-time-constant *upper* bound (the split
    // factor) for that region's extent to unroll it at all, and fold all
    // the per-position validity checks into a single guard around the
    // whole tile rather than one nested check per unrolled position.
    Var x{"x"}, xo{"xo"}, xi{"xi"};
    Func g{"g"}, f{"f"};

    g(x) = x * 2;
    f(x) = g(x) + 1;
    f.output_buffer().dim(0).set_min(0);

    f.split(x, xo, xi, 5, TailStrategy::PredicateStores).never_partition_all();
    g.compute_at(f, xo).align_bounds(x, 5).unroll(x);

    Module m = f.compile_to_module({});

    CountIfsInProduce checker("g");
    for (const LoweredFunc &lf : m.functions()) {
        lf.body.accept(&checker);
    }

    // The tile's extent is exactly the split factor: the enclosing tile
    // loop's own max bounds the consumer's extent from below, so the
    // ceiling-divide that rounds the region up to a multiple of the factor
    // is exact. BoundConstantExtentLoops must find that as an exact
    // constant, not just an upper bound, so the unrolled body needs no
    // guard at all.
    if (!check(checker.count == 0,
               "expected no guard inside the unrolled tile"))
        return 1;

    // Values must still come out right at and past the boundary, for
    // several sizes that aren't a multiple of the split factor.
    for (int w : {1, 4, 5, 6, 9, 11, 23}) {
        Buffer<int> out = f.realize({w});
        for (int i = 0; i < w; i++) {
            int expected = i * 2 + 1;
            if (out(i) != expected) {
                printf("out(%d) = %d instead of %d (w = %d)\n", i, out(i), expected, w);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}

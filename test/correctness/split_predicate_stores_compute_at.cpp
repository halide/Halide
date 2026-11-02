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
int count_ifs_in_produce(const Stmt &s, const std::string &name) {
    int count = 0;
    int depth = 0;
    visit_with(
        s,
        [&](auto *self, const ProducerConsumer *op) {
            bool inside = op->is_producer && op->name == name;
            depth += inside;
            self->visit_base(op);
            depth -= inside;
        },
        [&](auto *self, const IfThenElse *op) {
            if (depth > 0) {
                count++;
            }
            self->visit_base(op);
        });
    return count;
}

}  // namespace

int main(int argc, char **argv) {
    // A producer unrolled inside a PredicateStores split tile of its
    // consumer. Its extent depends on the consumer's extent, so unrolling
    // needs the split factor found as a constant bound.
    Var x{"x"}, xo{"xo"}, xi{"xi"};
    Func g{"g"}, f{"f"};

    g(x) = x * 2;
    f(x) = g(x) + 1;
    f.output_buffer().dim(0).set_min(0);

    f.split(x, xo, xi, 5, TailStrategy::PredicateStores).never_partition_all();
    g.compute_at(f, xo).align_bounds(x, 5).unroll(x);

    Module m = f.compile_to_module({});

    int ifs = 0;
    for (const LoweredFunc &lf : m.functions()) {
        ifs += count_ifs_in_produce(lf.body, "g");
    }

    // The extent is exactly the split factor, so the unrolled body needs no
    // guard.
    if (!check(ifs == 0,
               "expected no guard inside the unrolled tile"))
        return 1;

    // Check sizes that aren't a multiple of the split factor.
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

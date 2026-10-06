#include "Halide.h"
#include "expect_user_error.h"

#include <functional>
#include <stdio.h>

using namespace Halide;

// eager_inline() only accepts Funcs whose schedule is compatible with inlining
// (as for compute_inline()). Each scenario schedules the producer in a way that
// lowering rejects for an inlined Func, then asks to eagerly inline it.
void try_inline(const std::function<void(Func &, Func &)> &schedule) {
    Var x{"x"};
    Func producer{"producer"}, consumer{"consumer"};
    producer(x) = x * 2;
    consumer(x) = producer(x) + 1;
    schedule(producer, consumer);
    consumer.eager_inline(producer);
}

int main(int argc, char **argv) {
    if (!exceptions_enabled()) {
        printf("[SKIP] Halide was compiled without exceptions.\n");
        return 0;
    }

    Var x{"x"}, xo{"xo"}, xi{"xi"};
    int failures = 0;
    auto expect = [&](const char *name, const char *msg, const std::function<void(Func &, Func &)> &schedule) {
        failures += !expect_user_error(name, msg, [&]() { try_inline(schedule); });
    };

    expect("compute_root", ": it is scheduled compute_root() or compute_at()",
           [](Func &p, Func &) { p.compute_root(); });
    expect("compute_at", ": it is scheduled compute_root() or compute_at()",
           [&](Func &p, Func &c) { p.compute_at(c, x); });
    expect("store_root", ": it is scheduled store_root() or store_at()",
           [](Func &p, Func &) { p.store_root(); });
    expect("hoist_storage_root", ": it is scheduled hoist_storage_root() or hoist_storage()",
           [](Func &p, Func &) { p.hoist_storage_root(); });
    expect("compute_with", ": it is scheduled compute_with()",
           [&](Func &p, Func &c) { p.compute_with(c, x); });
    expect("deferred_compute_at", ": it is scheduled compute_root() or compute_at() at a LoopLevel that has not been set yet",
           [](Func &p, Func &) { p.compute_at(LoopLevel()); });
    expect("memoize", ": it is scheduled memoize()",
           [](Func &p, Func &) { p.memoize(); });
    expect("parallel", ": its loop over x.xo is scheduled parallel",
           [&](Func &p, Func &) { p.split(x, xo, xi, 4).parallel(xo); });
    expect("vectorize", ": its loop over x is scheduled vectorized",
           [&](Func &p, Func &) { p.vectorize(x); });
    expect("unroll", ": its loop over x.xi is scheduled unrolled",
           [&](Func &p, Func &) { p.split(x, xo, xi, 4).unroll(xi); });
    expect("gpu_blocks", ": its loop over x is scheduled as a GPU block or thread loop",
           [&](Func &p, Func &) { p.gpu_blocks(x); });

    if (failures != 0) {
        printf("%d scenario(s) failed to produce the expected error\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}

#include "Halide.h"
#include "check_call_graphs.h"
#include "test_sharding.h"

#include <cstdio>
#include <functional>
#include <vector>

namespace {

using namespace Halide;

int fib_fold2() {
    Func f(Int(32), "f"), g("g");
    Var x("x"), y("y");
    f(x, y) = select(x <= 1, x + y, likely(f(x - 1, y) + f(x - 2, y)));
    g(x, y) = f(x, y);
    f.compute_at(g, x).store_root().fold_storage(x, 2);
    g.bound(x, 0, 30).bound(y, 0, 8);

    Buffer<int> im = g.realize({30, 8});
    return check_image(im, [](int x, int y) {
        if (x <= 1) return x + y;
        int a = y, b = 1 + y;
        for (int i = 2; i <= x; i++) {
            int c = b + a;
            a = b;
            b = c;
        }
        return b;
    });
}

int multi_inner_injective_fold1() {
    Func f(Int(32), "f"), g("g");
    Var x("x"), y("y"), z("z");
    f(x, y, z) = select(x <= 0, y + z, likely(f(x - 1, y, z) + 1));
    g(x, y, z) = f(x, y, z);
    g.reorder(z, y, x);
    f.compute_at(g, x).store_root().fold_storage(x, 1);
    g.bound(x, 0, 16).bound(y, 0, 4).bound(z, 0, 4);

    Buffer<int> im = g.realize({16, 4, 4});
    return check_image(im, [](int x, int y, int z) { return x + y + z; });
}

int consumer_strided_nonfold_fold1() {
    Func f(Int(32), "f"), g("g");
    Var x("x"), y("y");
    f(x, y) = select(x <= 0, y, likely(f(x - 1, y) + 1));  // f(x,y) = x + y
    g(x, y) = f(x, 2 * y);
    f.compute_at(g, x).store_root().fold_storage(x, 1);
    g.bound(x, 0, 64).bound(y, 0, 8);

    Buffer<int> im = g.realize({64, 8});
    return check_image(im, [](int x, int y) { return x + 2 * y; });
}

// --- Cases that must be REFUSED (folding tighter than safe). Each asks for a
// fold factor below the true minimum; with the guards working, Halide rejects it
// (fold-factor-too-small) rather than silently corrupting. We expect a throw.

// lag-3 needs 3 slots; folding to 2 is unsafe.
int lag3_fold2_refused() {
    try {
        Func f(Int(32), "f"), g("g");
        Var x("x"), y("y");
        f(x, y) = select(x <= 2, x + y, likely(f(x - 3, y) + 1));
        g(x, y) = f(x, y);
        f.compute_at(g, x).store_root().fold_storage(x, 2);
        g.bound(x, 0, 64).bound(y, 0, 8);
        Buffer<int> im = g.realize({64, 8});
    } catch (const Halide::Error &) {
        return 0;
    }
    printf("  lag-3 was folded to 2 but should have been refused\n");
    return 1;
}

// Self-call broadcasts the non-fold dim to a fixed index: f(x,y) reads
// f(x-1, 0). Folding x to 1 is unsafe -- f(x,0) overwrites the buffer[0][0] slot
// that f(x, y>0) still needs to read as f(x-1, 0). Must be refused.
int self_broadcast_nonfold_refused() {
    try {
        Func f(Int(32), "f"), g("g");
        Var x("x"), y("y");
        f(x, y) = select(x <= 0, y, likely(f(x - 1, 0) + 1));
        g(x, y) = f(x, y);
        f.compute_at(g, x).store_root().fold_storage(x, 1);
        g.bound(x, 0, 64).bound(y, 0, 8);
        Buffer<int> im = g.realize({64, 8});
    } catch (const Halide::Error &) {
        return 0;
    }
    printf("  self-broadcast in non-fold dim was folded to 1 but should have been refused\n");
    return 1;
}

// Redundant stores in a non-inductive dimension: f is 1-D in x, but is produced
// inside an inner y loop (g reuses f(x) for every y), so f(x) is written once per
// y -- the same slot, redundantly. Folding x to 1 is unsafe: the second y would
// read the freshly-written f(x) as if it were f(x-1). The store is not injective
// over the inner y loop, so store_is_single_and_disjoint must refuse it (this is
// exactly the ParallelRVar-style duplicate-store falsification).
int redundant_nonfold_store_refused() {
    try {
        Func f(Int(32), "f"), g("g");
        Var x("x"), y("y");
        f(x) = select(x <= 0, 0, likely(f(x - 1) + 1));      // 1-D, f(x) = x
        g(x, y) = f(x);                                      // reused across y
        g.reorder(y, x);                                     // x outer, y inner
        f.compute_at(g, y).store_root().fold_storage(x, 1);  // re-produced per y
        g.bound(x, 0, 64).bound(y, 0, 8);
        Buffer<int> im = g.realize({64, 8});
    } catch (const Halide::Error &) {
        return 0;
    }
    printf("  redundant non-fold store folded to 1 but should have been refused\n");
    return 1;
}

}  // namespace

int main(int argc, char **argv) {
    struct Task {
        std::string desc;
        std::function<int()> fn;
    };

    std::vector<Task> tasks = {
        {"multi-lag (x-1, x-2), fold_storage 2", fib_fold2},
        {"multi-inner-dim recurrence, fold_storage 1", multi_inner_injective_fold1},
        {"strided non-fold consumer access, fold_storage 1", consumer_strided_nonfold_fold1},
        {"lag-3 folded to 2 is refused", lag3_fold2_refused},
        {"self-broadcast in non-fold dim folded to 1 is refused", self_broadcast_nonfold_refused},
        {"redundant non-fold store folded to 1 is refused", redundant_nonfold_store_refused},
    };

    using Sharder = Halide::Internal::Test::Sharder;
    Sharder sharder;
    for (size_t t = 0; t < tasks.size(); t++) {
        if (!sharder.should_run(t)) continue;
        const auto &task = tasks.at(t);
        std::cout << task.desc << "\n";
        if (task.fn() != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}

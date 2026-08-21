#include "Halide.h"

using namespace Halide;

// Regression test for a lost-wakeup deadlock in the async/thread-pool
// runtime, reduced from async_copy_chain.cpp. A worker blocked because a
// semaphore it needed was unavailable went to sleep on one of the two idle
// worker condition variables (the "A team" / "B team"), chosen by pool-size
// bookkeeping unrelated to why it slept. A semaphore release only ever woke
// the A team, so a semaphore-blocked worker that happened to be on the B
// team was never woken by the release that made its job runnable, and every
// thread ended up parked forever.
//
// The bug is a genuine race, so it does not reproduce reliably from a single
// realization. It reproduces most readily when this specific sequence of
// nested-async pipeline shapes runs one after another in the same process,
// so we repeat the whole sequence to raise the probability of hitting it. A
// reintroduced deadlock hangs forever, so CI's watchdog will catch it even
// at a modest iteration count.
//
// The race requires a semaphore-blocked worker to be demoted to the B team,
// which only happens when the pool is small enough to keep reshuffling teams
// under load. A large pool stays entirely on the A team and never exposes
// the bug, so we pin the thread count low, where the reintroduced deadlock
// reproduces on the vast majority of runs.
static constexpr int num_threads = 3;

Var x, y;

void make_pipeline(Func &A, Func &B) {
    A(x, y) = x + y;
    B(x, y) = A(x, y);
}

void check(Func f, const Target &target) {
    Buffer<int> out = f.realize({256, 256}, target);
    out.for_each_element([&](int x, int y) {
        if (out(x, y) != x + y) {
            printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), x + y);
            exit(1);
        }
    });
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment().with_feature(Target::EnableBacktraces);
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly does not support async() yet.\n");
        return 0;
    }

    Halide::Internal::JITSharedRuntime::set_num_threads(num_threads);

    const int iterations = 25;
    for (int i = 0; i < iterations; i++) {
        {
            Func A, B;
            make_pipeline(A, B);
            A.store_root().compute_at(B, y).fold_storage(y, 2).async();
            check(B, target);
        }
        {
            Func A, B;
            make_pipeline(A, B);
            A.store_root().compute_at(B, y).fold_storage(y, 2).async();
            A.in().store_root().compute_at(B, y).fold_storage(y, 2).async().copy_to_host();
            check(B, target);
        }
        {
            Func A, B;
            make_pipeline(A, B);
            A.store_root().compute_at(A.in(), Var::outermost()).fold_storage(y, 2).async();
            A.in().store_root().compute_at(B, y).fold_storage(y, 2).async().copy_to_host();
            check(B, target);
        }
        {
            Func A, B;
            make_pipeline(A, B);
            A.store_root().compute_at(B, y).fold_storage(y, 2).async();
            A.in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();
            A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();
            check(B, target);
        }
        {
            Func A, B;
            make_pipeline(A, B);
            A.store_root().compute_at(A.in(), Var::outermost()).fold_storage(y, 2).async();
            A.in().store_root().compute_at(A.in().in(), Var::outermost()).fold_storage(y, 2).copy_to_host().async();
            A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();
            check(B, target);
        }
    }

    printf("Success!\n");
    return 0;
}

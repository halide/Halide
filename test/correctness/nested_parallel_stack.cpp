#include "Halide.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdio.h>
#include <thread>

using namespace Halide;

// A minimal counting semaphore (std::counting_semaphore is C++20).
class Semaphore {
    std::mutex mutex;
    std::condition_variable cond;
    int count = 0;

public:
    void release() {
        std::lock_guard<std::mutex> lock(mutex);
        count++;
        cond.notify_one();
    }
    void acquire() {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this] { return count > 0; });
        count--;
    }
};

// Two semaphores, selected by the parity of the extern's argument, so that the
// test can control exactly which inner task is allowed to complete when.
static Semaphore semaphores[2];

extern "C" HALIDE_EXPORT_SYMBOL int nested_parallel_stack_acquire(int x) {
    semaphores[x & 1].acquire();
    return x;
}
HalideExtern_1(int, nested_parallel_stack_acquire, int);

// Measure how deeply task execution nests on a single thread.
static thread_local int t_depth = 0;
static std::atomic<int> max_depth{0};

static int counting_do_task(JITUserContext *user_context,
                            int (*f)(JITUserContext *, int, uint8_t *),
                            int idx, uint8_t *closure) {
    int d = ++t_depth;
    int prev = max_depth.load();
    while (d > prev && !max_depth.compare_exchange_weak(prev, d)) {
    }
    int result = f(user_context, idx, closure);
    --t_depth;
    return result;
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support threads.\n");
        return 0;
    }

    // An outer parallel loop over y wrapping an inner parallel loop over x.
    // Each inner task blocks in the extern until released below.
    Var x("x"), y("y");
    Func g("g"), out("out");
    g(x, y) = nested_parallel_stack_acquire(x);
    out(x, y) = g(x, y);

    out.parallel(y);
    g.compute_at(out, y).parallel(x);

    // Exactly two threads: one becomes the owner that keeps descending, the
    // other clears each level behind it. The shared jit runtime is created
    // lazily, so it must exist before we can set the thread count on it.
    out.compile_jit();
    Internal::JITSharedRuntime::set_num_threads(2);

    const int N = 64;

    // Drive completion order from a thread outside the pool. Each round,
    // release the even inner task first, then the odd one. That makes the
    // owner finish its first inner task and start another outer task while the
    // odd task is still in flight, so it descends instead of unwinding.
    std::thread poster([&]() {
        for (int i = 0; i < N; i++) {
            semaphores[0].release();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            semaphores[1].release();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // Drain any stragglers so the pipeline can't hang. Over-releasing a
        // counting semaphore is harmless.
        for (int i = 0; i < 4 * N; i++) {
            semaphores[0].release();
            semaphores[1].release();
        }
    });

    JITUserContext context;
    context.handlers.custom_do_task = counting_do_task;
    out.realize(&context, {2, N});
    poster.join();

    // The thread pool bounds how many jobs a thread may own at once, so the
    // nesting depth should not grow with N.
    const int limit = 8;
    if (max_depth.load() > limit) {
        printf("Nested parallel loops recursed %d deep (limit %d). A single "
               "thread's stack accumulates one frame per outer task.\n",
               max_depth.load(), limit);
        return 1;
    }

    printf("Success!\n");
    return 0;
}

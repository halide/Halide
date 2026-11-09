#include "Halide.h"

#include <stdio.h>
#include <thread>
#include <vector>

using namespace Halide;
using Halide::Internal::JITSharedRuntime;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: Check failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

namespace {

using keep_awake_fn = int (*)(bool);

// The number of references held through JITSharedRuntime. Only meaningful
// when no other thread is changing it.
int front_end_count() {
    int c = JITSharedRuntime::thread_pool_keep_awake(true);
    CHECK(c >= 1);
    CHECK(JITSharedRuntime::thread_pool_keep_awake(false) == c - 1);
    return c - 1;
}

// The keep-awake count of a shared runtime's thread pool.
int runtime_count(keep_awake_fn f) {
    int c = f(true);
    CHECK(c >= 1);
    CHECK(f(false) == c - 1);
    return c - 1;
}

keep_awake_fn find_keep_awake(const Target &t) {
    return (keep_awake_fn)JITSharedRuntime::find_symbol(t, "halide_thread_pool_keep_awake");
}

// A small pipeline with nested parallelism and an async producer.
Callable make_pipeline(const Target &t, Param<int> &offset) {
    Var x, y, xo, xi;
    Func producer, consumer, output;
    producer(x, y) = x * y + offset;
    consumer(x, y) = producer(x - 1, y) + producer(x + 1, y);
    output(x, y) = consumer(x, y) + 1;

    output.parallel(y);
    consumer.compute_at(output, y).split(x, xo, xi, 4).parallel(xo);
    producer.compute_at(output, y).async();
    return output.compile_to_callable({offset}, t);
}

void run(Callable &c, int offset) {
    Buffer<int> out(32, 8);
    CHECK(c(offset, out) == 0);
    out.for_each_element([&](int x, int y) {
        CHECK(out(x, y) == 2 * x * y + 2 * offset + 1);
    });
}

}  // namespace

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (t.arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly does not support async() yet.\n");
        return 0;
    }

    Param<int> offset;
    {
        // Hold a reference before the shared runtime exists. It's applied
        // when the runtime is created.
        ThreadPoolKeepAwake early;
        CHECK(front_end_count() == 1);

        Callable pipeline = make_pipeline(t, offset);
        run(pipeline, 0);
        keep_awake_fn keep_awake = find_keep_awake(t);
        CHECK(keep_awake);
        CHECK(runtime_count(keep_awake) == 1);

        // References may nest and live on other threads. The shared runtime
        // holds one reference on behalf of all of them.
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; i++) {
            threads.emplace_back([&pipeline, i]() {
                ThreadPoolKeepAwake a;
                {
                    ThreadPoolKeepAwake b;
                    for (int j = 0; j < 100; j++) {
                        run(pipeline, i + j);
                    }
                }
                for (int j = 0; j < 100; j++) {
                    run(pipeline, i - j);
                }
            });
        }
        for (auto &th : threads) {
            th.join();
        }
        CHECK(front_end_count() == 1);
        CHECK(runtime_count(keep_awake) == 1);

        // Many tiny parallel loops, with and without an extra reference.
        for (int i = 0; i < 500; i++) {
            if (i % 100 < 50) {
                ThreadPoolKeepAwake extra;
                run(pipeline, i);
            } else {
                run(pipeline, i);
            }
        }

        // Change the number of threads while held.
        int old_threads = JITSharedRuntime::set_num_threads(1);
        for (int n : {1, 4, 2, 8, 3}) {
            JITSharedRuntime::set_num_threads(n);
            CHECK(JITSharedRuntime::get_num_threads() == n);
            for (int i = 0; i < 50; i++) {
                run(pipeline, i);
            }
        }
        JITSharedRuntime::set_num_threads(old_threads);

        // Shut down the thread pool while held. The count survives.
        using shutdown_fn = void (*)();
        shutdown_fn shutdown =
            (shutdown_fn)JITSharedRuntime::find_symbol(t, "halide_shutdown_thread_pool");
        CHECK(shutdown);
        shutdown();
        CHECK(runtime_count(keep_awake) == 1);
        run(pipeline, 7);

        // Releasing the shared runtimes releases the old runtime's reference,
        // and a new runtime picks it up again. The old runtime stays alive
        // because the pipeline compiled with it still refers to it.
        JITSharedRuntime::release_all();
        CHECK(runtime_count(keep_awake) == 0);
        run(pipeline, 8);
        CHECK(front_end_count() == 1);

        Callable new_pipeline = make_pipeline(t, offset);
        run(new_pipeline, 9);
        keep_awake_fn new_keep_awake = find_keep_awake(t);
        CHECK(new_keep_awake && new_keep_awake != keep_awake);
        CHECK(runtime_count(new_keep_awake) == 1);
        CHECK(runtime_count(keep_awake) == 0);

        // Releasing a nested reference doesn't release the runtime's.
        {
            ThreadPoolKeepAwake nested;
        }
        CHECK(runtime_count(new_keep_awake) == 1);
    }
    CHECK(front_end_count() == 0);
    keep_awake_fn keep_awake = find_keep_awake(t);
    CHECK(keep_awake);
    CHECK(runtime_count(keep_awake) == 0);

#if HALIDE_WITH_EXCEPTIONS
    if (Halide::exceptions_enabled()) {
        // Releasing a reference that isn't held is an error.
        bool threw = false;
        try {
            JITSharedRuntime::thread_pool_keep_awake(false);
        } catch (const Halide::CompileError &e) {
            printf("Expected error: %s\n", e.what());
            threw = true;
        }
        CHECK(threw);
        CHECK(front_end_count() == 0);
        CHECK(runtime_count(keep_awake) == 0);
    }
#endif

    printf("Success!\n");
    return 0;
}

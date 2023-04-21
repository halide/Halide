#include "Halide.h"
#include "halide_benchmark.h"
#include <cstdio>
#include <functional>
#include <thread>

#ifdef __GNUC__
// We don't want to emit deprecation warnings for this test
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

/** \file Test to demonstrate using JIT across multiple threads with
 * varying parameters passed to realizations. Performance is tested
 * by comparing a technique that recompiles vs one that should not.
 */

using namespace Halide;
using namespace Halide::Tools;

struct test_func {
    Param<int32_t> p;
    ImageParam in{Int(32), 1};
    Func f;
    Var x;

    test_func() {
        Expr big = 0;
        for (int i = 0; i < 75; i++) {
            big += p;
        }
        Func inner;
        inner(x) = x * in(clamp(x, 0, 9)) + big;
        f(x) = inner(x - 1) + inner(x) + inner(x + 1);
        inner.compute_at(f, x);

        // The Halide compiler is threadsafe, with the important caveat
        // that mutable objects like Funcs and ImageParams cannot be
        // shared across thread boundaries without being guarded by a
        // mutex. Since we don't share any such objects here, we don't
        // need any synchronization
        f.compile_jit();
    }

    test_func(const test_func &copy) = delete;
    test_func &operator=(const test_func &) = delete;
    test_func(test_func &&) = delete;
    test_func &operator=(test_func &&) = delete;
};

Buffer<int32_t> bufs[16];

void separate_func_per_thread_executor(int index) {
    test_func test;

    test.p.set(index);
    test.in.set(bufs[index]);
    for (int i = 0; i < 10; i++) {
        Buffer<int32_t> result = test.f.realize({10});
        for (int j = 0; j < 10; j++) {
            int64_t left = ((j - 1) * (int64_t)bufs[index](std::min(std::max(0, j - 1), 9)) + index * 75);
            int64_t middle = (j * (int64_t)bufs[index](std::min(std::max(0, j), 9)) + index * 75);
            int64_t right = ((j + 1) * (int64_t)bufs[index](std::min(std::max(0, j + 1), 9)) + index * 75);
            assert(result(j) == (int32_t)(left + middle + right));
        }
    }
}

void separate_func_per_thread() {
    std::thread threads[16];

    for (auto &thread : threads) {
        thread = std::thread(separate_func_per_thread_executor,
                             (int)(&thread - threads));
    }

    for (auto &thread : threads) {
        thread.join();
    }
}

void same_func_per_thread_executor(int index, test_func &test) {
    for (int i = 0; i < 10; i++) {
        Buffer<int32_t> result = test.f.realize({10}, get_jit_target_from_environment(),
                                                {{test.p, index},
                                                 {test.in, bufs[index]}});
        for (int j = 0; j < 10; j++) {
            int64_t left = ((j - 1) * (int64_t)bufs[index](std::min(std::max(0, j - 1), 9)) + index * 75);
            int64_t middle = (j * (int64_t)bufs[index](std::min(std::max(0, j), 9)) + index * 75);
            int64_t right = ((j + 1) * (int64_t)bufs[index](std::min(std::max(0, j + 1), 9)) + index * 75);
            assert(result(j) == (int32_t)(left + middle + right));
        }
    }
}

void same_func_per_thread() {
    std::thread threads[16];
    test_func test;

    for (auto &thread : threads) {
        thread = std::thread(same_func_per_thread_executor,
                             (int)(&thread - threads), std::ref(test));
    }

    for (auto &thread : threads) {
        thread.join();
    }
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    for (auto &buf : bufs) {
        buf = Buffer<int32_t>(10);
        for (int i = 0; i < 10; i++) {
            buf(i) = std::rand();
        }
    }

    double separate_time = benchmark(separate_func_per_thread);
    printf("Separate compilations time: %fs.\n", separate_time);

    double same_time = benchmark(same_func_per_thread);
    printf("One compilation time: %fs.\n", same_time);

    assert(same_time < separate_time);

    printf("Success!\n");
    return 0;
}

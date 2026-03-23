#include "Halide.h"
#include "halide_benchmark.h"
#include <cstdio>
#include <functional>
#include <thread>

/** \file Test to demonstrate using JIT across multiple threads with
 * varying parameters passed to realizations. Performance is tested
 * by comparing a technique that recompiles vs one that should not.
 */

using namespace Halide;
using namespace Halide::Tools;

struct test_func {
    Param<int32_t> p;
    ImageParam in{Int(32), 1};
    Func func;
    Var x;
    std::function<int(Buffer<int32_t, 1>, int32_t, Buffer<int32_t, 1>)> f;

    test_func() {
        Expr big = 0;
        for (int i = 0; i < 75; i++) {
            big += p;
        }
        Func inner;
        inner(x) = x * in(clamp(x, 0, 9)) + big;
        func(x) = inner(x - 1) + inner(x) + inner(x + 1);
        inner.compute_at(func, x);

        // The Halide compiler is threadsafe, with the important caveat
        // that mutable objects like Funcs and ImageParams cannot be
        // shared across thread boundaries without being guarded by a
        // mutex. Since we don't share any such objects here, we don't
        // need any synchronization
        f = func.compile_to_callable({in, p}).make_std_function<Buffer<int32_t, 1>, int32_t, Buffer<int32_t, 1>>();
    }

    test_func(const test_func &copy) = delete;
    test_func &operator=(const test_func &) = delete;
    test_func(test_func &&) = delete;
    test_func &operator=(test_func &&) = delete;
};

Buffer<int32_t> bufs[16];

void separate_func_per_thread_executor(int index) {
    test_func test;

    Buffer<int32_t> output(10);
    for (int i = 0; i < 10; i++) {
        int result = test.f(bufs[index], index, output);
        assert(result == 0);
        for (int j = 0; j < 10; j++) {
            int64_t left = ((j - 1) * (int64_t)bufs[index](std::min(std::max(0, j - 1), 9)) + index * 75);
            int64_t middle = (j * (int64_t)bufs[index](std::min(std::max(0, j), 9)) + index * 75);
            int64_t right = ((j + 1) * (int64_t)bufs[index](std::min(std::max(0, j + 1), 9)) + index * 75);
            assert(output(j) == (int32_t)(left + middle + right));
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
    Buffer<int32_t> output(10);
    for (int i = 0; i < 10; i++) {
        int result = test.f(bufs[index], index, output);
        assert(result == 0);
        for (int j = 0; j < 10; j++) {
            int64_t left = ((j - 1) * (int64_t)bufs[index](std::min(std::max(0, j - 1), 9)) + index * 75);
            int64_t middle = (j * (int64_t)bufs[index](std::min(std::max(0, j), 9)) + index * 75);
            int64_t right = ((j + 1) * (int64_t)bufs[index](std::min(std::max(0, j + 1), 9)) + index * 75);
            assert(output(j) == (int32_t)(left + middle + right));
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

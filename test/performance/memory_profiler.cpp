#include "Halide.h"
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;

// Exits with code 1 if the profiler's stats for f don't match the
// expectations. exp_heap_peak is a range [min, max]; for an exact
// expectation pass it twice.
void check(const ProfilerScope &scope, const Func &f,
           int exp_heap_peak_min, int exp_heap_peak_max,
           int exp_num_mallocs, int exp_malloc_avg, int exp_stack_peak) {
    const halide_profiler_func_stats *fs = scope.func_stats(f);
    if (!fs) {
        printf("No profiler stats found for %s\n", f.name().c_str());
        exit(1);
    }
    int heap_peak = 0, num_mallocs = 0, malloc_avg = 0;
    if (fs->num_allocs > 0) {
        heap_peak = (int)fs->memory_peak;
        num_mallocs = (int)fs->num_allocs;
        malloc_avg = (int)(fs->memory_total / fs->num_allocs);
    }
    int stack_peak = (int)fs->stack_peak;

    if (heap_peak < exp_heap_peak_min || heap_peak > exp_heap_peak_max) {
        printf("Peak heap was %d, expected in [%d, %d]\n",
               heap_peak, exp_heap_peak_min, exp_heap_peak_max);
        exit(1);
    }
    if (num_mallocs != exp_num_mallocs) {
        printf("Num of mallocs was %d, expected %d\n",
               num_mallocs, exp_num_mallocs);
        exit(1);
    }
    if (malloc_avg != exp_malloc_avg) {
        printf("Malloc average was %d, expected %d\n",
               malloc_avg, exp_malloc_avg);
        exit(1);
    }
    if (stack_peak != exp_stack_peak) {
        printf("Stack peak was %d, expected %d\n",
               stack_peak, exp_stack_peak);
        exit(1);
    }
}

template<typename Fn>
void run_case(const char *desc, Fn body) {
    printf("Running %s...\n", desc);
    body();
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    const Target t = target.with_feature(Target::Profile);

    Var x("x"), y("y");

    run_case("simple stack allocation test", [&]() {
        // On stack allocation (≤ 1024*16, and g1 is constant 64x64).
        const int size_x = 64, size_y = 64;
        Func f1("f_1"), g1("g_1");
        g1(x, y) = x;
        f1(x, y) = g1(x % size_x, y % size_y);
        g1.compute_root();

        Pipeline pipe(f1);
        ProfilerScope scope(pipe);
        pipe.realize({size_x, size_y}, t);
        check(scope, g1, 0, 0, 0, 0, size_x * size_y * (int)sizeof(int));
    });

    run_case("simple heap allocation test 1", [&]() {
        // On heap allocation (> 1024*16).
        const int size_x = 1000, size_y = 1000;
        Func f2("f_2"), g2("g_2");
        g2(x, y) = x;
        f2(x, y) = g2(x - 1, y) + g2(x, y - 1);
        g2.compute_root();

        Pipeline pipe(f2);
        ProfilerScope scope(pipe);
        pipe.realize({size_x, size_y}, t);
        int total = (size_x + 1) * (size_y + 1) * (int)sizeof(int);
        check(scope, g2, total, total, 1, total, 0);
    });

    run_case("heap allocate condition is always false test", [&]() {
        Func f3("f_3"), g3("g_3");
        g3(x, y) = x * y;
        f3(x, y) = select(1 == 2, g3(x - 1, y), 0);
        g3.compute_root();

        Pipeline pipe(f3);
        ProfilerScope scope(pipe);
        pipe.realize({1000, 1000}, t);
        check(scope, g3, 0, 0, 0, 0, 0);
    });

    run_case("stack allocate condition is always false test", [&]() {
        Func f3("f_3"), g3("g_3");
        g3(x, y) = x * y;
        f3(x, y) = select(1 == 2, g3((x - 1) % 10, y % 10), 0);
        g3.compute_root();

        Pipeline pipe(f3);
        ProfilerScope scope(pipe);
        pipe.realize({1000, 1000}, t);
        check(scope, g3, 0, 0, 0, 0, 0);
    });

    run_case("allocate with non-trivial condition test", [&]() {
        const int size_x = 10000;
        Param<bool> toggle1, toggle2;
        Func g4("g_4"), f4("f_4"), f5("f_5"), f6("f_6");

        g4(x) = sin(x);
        f4(x) = g4(x) + 1;
        f5(x) = g4(x) + 2;
        f6(x) = select(toggle1, f4(x), 0) + select(toggle2, f5(x), 0);

        g4.compute_root();
        f4.compute_root();
        f5.compute_root();

        Pipeline pipe(f6);

        const int total = size_x * (int)sizeof(float);
        const struct {
            bool t1, t2;
            int exp_heap, exp_mallocs, exp_avg;
        } cases[] = {
            {true, true, total, 1, total},
            {true, false, total, 1, total},
            {false, true, total, 1, total},
            {false, false, 0, 0, 0},
        };
        for (auto &c : cases) {
            // A fresh scope per run, so that stats don't accumulate.
            ProfilerScope scope(pipe);
            toggle1.set(c.t1);
            toggle2.set(c.t2);
            pipe.realize({size_x}, t);
            check(scope, g4, c.exp_heap, c.exp_heap, c.exp_mallocs, c.exp_avg, 0);
        }
    });

    run_case("allocate within loop test", [&]() {
        const int size_x = 1200, size_y = 1000;
        Func f7("f_7"), f8("f_8"), g5("g_5");
        g5(x, y) = x * y;
        f7(x, y) = g5(x, y);
        f8(x, y) = g5(x, y) + f7(x, y);

        g5.store_at(f8, y).compute_at(f8, y);
        f7.compute_at(f8, y);

        Pipeline pipe(f8);
        ProfilerScope scope(pipe);
        pipe.realize({size_x, size_y}, t);

        int peak = size_x * (int)sizeof(int);
        int total = size_x * size_y * (int)sizeof(int);
        check(scope, g5, peak, peak, size_y, total / size_y, 0);
    });

    run_case("parallel allocate test", [&]() {
        const int size_x = 1200, size_y = 1000;
        Func f9("f_9"), f10("f_10"), g6("g_6");
        g6(x, y) = x * y;
        f9(x, y) = g6(x, y);
        f10(x, y) = g6(x, y) + f9(x, y);

        g6.store_at(f10, y).compute_at(f10, y);
        f9.compute_at(f10, y);
        f10.parallel(y);

        Pipeline pipe(f10);
        ProfilerScope scope(pipe);
        pipe.realize({size_x, size_y}, t);

        int min_heap = size_x * (int)sizeof(int);
        int total = size_x * size_y * (int)sizeof(int);
        check(scope, g6, min_heap, total, size_y, total / size_y, 0);
    });

    run_case("simple heap allocation test 2", [&]() {
        // On heap allocation (> 1024*16, g7 is constant 65x64).
        const int size_x = 65, size_y = 64;
        Func f11("f_11"), g7("g_7");
        g7(x, y) = x;
        f11(x, y) = g7(x % size_x, y % size_y);
        g7.compute_root();

        Pipeline pipe(f11);
        ProfilerScope scope(pipe);
        pipe.realize({size_x, size_y}, t);
        int total = size_x * size_y * (int)sizeof(int);
        check(scope, g7, total, total, 1, total, 0);
    });

    run_case("parallel stack allocation test", [&]() {
        const int size_x = 10, size_y = 10;
        Func f12("f_12"), g8("g_8");
        g8(x, y) = x;
        f12(x, y) = g8(x % size_x, y % size_y);
        g8.store_at(f12, y).compute_at(f12, y);
        f12.parallel(y);

        Pipeline pipe(f12);
        ProfilerScope scope(pipe);
        pipe.realize({size_x, size_y}, t);
        check(scope, g8, 0, 0, 0, 0, size_x * size_y * (int)sizeof(int));
    });

    printf("Success!\n");
    return 0;
}

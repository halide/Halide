#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int heap_peak = 0;
int num_mallocs = 0;
int malloc_avg = 0;
int stack_peak = 0;

void reset_stats() {
    heap_peak = 0;
    num_mallocs = 0;
    malloc_avg = 0;
    stack_peak = 0;
}

void my_print(JITUserContext *, const char *msg) {
    float this_ms, this_threads;
    int idx, this_percentage, this_heap_peak;
    int this_num_mallocs, this_malloc_avg, this_stack_peak;
    int val;

    // printf("%s", msg);
    val = sscanf(msg, " g_%d: %fms (%d%%) threads: %f peak: %d num: %d avg: %d",
                 &idx, &this_ms, &this_percentage, &this_threads, &this_heap_peak,
                 &this_num_mallocs, &this_malloc_avg);
    if (val == 7) {
        heap_peak = this_heap_peak;
        num_mallocs = this_num_mallocs;
        malloc_avg = this_malloc_avg;
    }

    val = sscanf(msg, " g_%d: %fms (%d%%) peak: %d num: %d avg: %d",
                 &idx, &this_ms, &this_percentage, &this_heap_peak,
                 &this_num_mallocs, &this_malloc_avg);
    if (val == 6) {
        heap_peak = this_heap_peak;
        num_mallocs = this_num_mallocs;
        malloc_avg = this_malloc_avg;
    }

    val = sscanf(msg, " g_%d: %fms (%d%%) threads: %f stack: %d",
                 &idx, &this_ms, &this_percentage, &this_threads, &this_stack_peak);
    if (val == 5) {
        stack_peak = this_stack_peak;
    }

    val = sscanf(msg, " g_%d: %fms (%d%%) stack: %d",
                 &idx, &this_ms, &this_percentage, &this_stack_peak);
    if (val == 4) {
        stack_peak = this_stack_peak;
    }
}

// Return 0 if there is no error found
int check_error(int exp_heap_peak, int exp_num_mallocs,
                int exp_malloc_avg, int exp_stack_peak) {
    /*printf("Memory heap_peak: %d bytes, num_mallocs: %d, malloc_avg: %d, "
            "stack_peak: %d\n", heap_peak, num_mallocs, malloc_avg, stack_peak);*/
    if (heap_peak != exp_heap_peak) {
        printf("Peak heap was %d instead of %d\n", heap_peak, exp_heap_peak);
        return 1;
    }
    if (num_mallocs != exp_num_mallocs) {
        printf("Num of mallocs was %d instead of %d\n", num_mallocs, exp_num_mallocs);
        return 1;
    }
    if (malloc_avg != exp_malloc_avg) {
        printf("Malloc average was %d instead of %d\n", malloc_avg, exp_malloc_avg);
        return 1;
    }
    if (stack_peak != exp_stack_peak) {
        printf("Stack peak was %d instead of %d\n", stack_peak, exp_stack_peak);
        return 1;
    }
    return 0;
}

// Return 0 if there is no error found
int check_error_parallel(int min_heap_peak, int max_heap_peak, int exp_num_mallocs,
                         int exp_malloc_avg, int exp_stack_peak) {
    /*printf("Memory heap_peak: %d bytes, num_mallocs: %d, malloc_avg: %d, "
            "stack_peak: %d\n", heap_peak, num_mallocs, malloc_avg, stack_peak);*/
    if (heap_peak < min_heap_peak || heap_peak > max_heap_peak) {
        printf("Peak heap was %d which was outside the range of [%d, %d]\n",
               heap_peak, min_heap_peak, max_heap_peak);
        return 1;
    }
    if (num_mallocs != exp_num_mallocs) {
        printf("Num of mallocs was %d instead of %d\n", num_mallocs, exp_num_mallocs);
        return 1;
    }
    if (malloc_avg != exp_malloc_avg) {
        printf("Malloc average was %d instead of %d\n", malloc_avg, exp_malloc_avg);
        return 1;
    }
    if (stack_peak != exp_stack_peak) {
        printf("Stack peak was %d instead of %d\n", stack_peak, exp_stack_peak);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    Target t = get_jit_target_from_environment().with_feature(Target::Profile);

    Var x("x"), y("y");

    {
        printf("Running simple stack allocation test...\n");
        // On stack allocation (less than or equal to 1024*16 and buffer g1 size is constant 64x64)
        const int size_x = 64;
        const int size_y = 64;
        Func f1("f_1"), g1("g_1");
        g1(x, y) = x;
        f1(x, y) = g1(x % size_x, y % size_y);
        g1.compute_root();

        f1.jit_handlers().custom_print = my_print;

        reset_stats();
        f1.realize({size_x, size_y}, t);
        int stack_size = size_x * size_y * sizeof(int);
        if (check_error(0, 0, 0, stack_size) != 0) {
            return 1;
        }
    }

    {
        printf("Running simple heap allocation test 1...\n");
        // On heap allocation (bigger than 1024*16)
        const int size_x = 1000;
        const int size_y = 1000;

        Func f2("f_2"), g2("g_2");
        g2(x, y) = x;
        f2(x, y) = g2(x - 1, y) + g2(x, y - 1);
        g2.compute_root();

        f2.jit_handlers().custom_print = my_print;

        reset_stats();
        f2.realize({size_x, size_y}, t);
        int total = (size_x + 1) * (size_y + 1) * sizeof(int);
        if (check_error(total, 1, total, 0) != 0) {
            return 1;
        }
    }

    {
        printf("Running heap allocate condition is always false test...\n");
        // Allocate condiiton is always false
        Func f3("f_3"), g3("g_3");
        g3(x, y) = x * y;
        f3(x, y) = select(1 == 2, g3(x - 1, y), 0);
        g3.compute_root();

        f3.jit_handlers().custom_print = my_print;

        reset_stats();
        f3.realize({1000, 1000}, t);
        if (check_error(0, 0, 0, 0) != 0) {
            return 1;
        }
    }

    {
        printf("Running stack allocate condition is always false test...\n");
        // Allocate condiiton is always false
        Func f3("f_3"), g3("g_3");
        g3(x, y) = x * y;
        f3(x, y) = select(1 == 2, g3((x - 1) % 10, y % 10), 0);
        g3.compute_root();

        f3.jit_handlers().custom_print = my_print;

        reset_stats();
        f3.realize({1000, 1000}, t);
        if (check_error(0, 0, 0, 0) != 0) {
            return 1;
        }
    }

    {
        printf("Running allocate with non-trivial condition test...\n");

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

        f6.jit_handlers().custom_print = my_print;

        int total = 0;

        reset_stats();
        toggle1.set(true);
        toggle2.set(true);
        f6.realize({size_x}, t);
        total = size_x * sizeof(float);
        if (check_error(total, 1, total, 0) != 0) {
            return 1;
        }

        reset_stats();
        toggle1.set(true);
        toggle2.set(false);
        f6.realize({size_x}, t);
        total = size_x * sizeof(float);
        if (check_error(total, 1, total, 0) != 0) {
            return 1;
        }

        reset_stats();
        toggle1.set(false);
        toggle2.set(true);
        f6.realize({size_x}, t);
        total = size_x * sizeof(float);
        if (check_error(total, 1, total, 0) != 0) {
            return 1;
        }

        reset_stats();
        toggle1.set(false);
        toggle2.set(false);
        f6.realize({size_x}, t);
        if (check_error(0, 0, 0, 0) != 0) {
            return 1;
        }
    }

    {
        printf("Running allocate within loop test...\n");
        const int size_x = 1200;
        const int size_y = 1000;

        Func f7("f_7"), f8("f_8"), g5("g_5");
        g5(x, y) = x * y;
        f7(x, y) = g5(x, y);
        f8(x, y) = g5(x, y) + f7(x, y);

        g5.store_at(f8, y).compute_at(f8, y);
        f7.compute_at(f8, y);

        f8.jit_handlers().custom_print = my_print;

        reset_stats();
        f8.realize({size_x, size_y}, t);
        int peak = size_x * sizeof(int);
        int total = size_x * size_y * sizeof(int);
        if (check_error(peak, size_y, total / size_y, 0) != 0) {
            return 1;
        }
    }

    {
        printf("Running parallel allocate test...\n");
        const int size_x = 1200;
        const int size_y = 1000;

        Func f9("f_9"), f10("f_10"), g6("g_6");
        g6(x, y) = x * y;
        f9(x, y) = g6(x, y);
        f10(x, y) = g6(x, y) + f9(x, y);

        g6.store_at(f10, y).compute_at(f10, y);
        f9.compute_at(f10, y);

        f10.parallel(y);

        f10.jit_handlers().custom_print = my_print;

        reset_stats();
        f10.realize({size_x, size_y}, t);
        int min_heap_peak = size_x * sizeof(int);
        int total = size_x * size_y * sizeof(int);
        if (check_error_parallel(min_heap_peak, total, size_y, total / size_y, 0) != 0) {
            return 1;
        }
    }

    {
        printf("Running simple heap allocation test 2...\n");
        // On heap allocation (bigger than 1024*16 and buffer g7 size is constant 65x64)
        const int size_x = 65;
        const int size_y = 64;
        Func f11("f_11"), g7("g_7");
        g7(x, y) = x;
        f11(x, y) = g7(x % size_x, y % size_y);
        g7.compute_root();

        f11.jit_handlers().custom_print = my_print;

        reset_stats();
        f11.realize({size_x, size_y}, t);
        int total = size_x * size_y * sizeof(int);
        if (check_error(total, 1, total, 0) != 0) {
            return 1;
        }
    }

    {
        printf("Running parallel stack allocation test...\n");
        const int size_x = 10;
        const int size_y = 10;
        Func f12("f_12"), g8("g_8");
        g8(x, y) = x;
        f12(x, y) = g8(x % size_x, y % size_y);
        g8.store_at(f12, y).compute_at(f12, y);

        f12.parallel(y);

        f12.jit_handlers().custom_print = my_print;

        reset_stats();
        f12.realize({size_x, size_y}, t);
        int stack_size = size_x * size_y * sizeof(int);
        if (check_error(0, 0, 0, stack_size) != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}

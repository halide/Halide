#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int memory_current = 0;
int memory_peak = 0;
int memory_total = 0;

void reset_stats() {
    memory_current = 0;
    memory_peak = 0;
    memory_total = 0;
}

void my_print(void *, const char *msg) {
    float this_ms;
    int idx, this_percentage, this_memory_current, this_memory_peak, this_memory_total;
    int val = sscanf(msg, " g_%d: %fms (%d%%) (%d, %d, %d",
        &idx, &this_ms, &this_percentage, &this_memory_current, &this_memory_peak, &this_memory_total);
    //printf("%s\n", msg);
    if (val == 6) {
        memory_current = this_memory_current;
        memory_peak = this_memory_peak;
        memory_total = this_memory_total;
    }
}

// Return 0 if there is no error found
int check_error(int expected_current, int expected_peak, int expected_total) {
    /*printf("Memory current: %d bytes, peak: %d bytes, total: %d bytes\n",
            memory_current, memory_peak, memory_total);*/
    if (memory_current != expected_current) {
        printf("Current memory was %d instead of %d\n", memory_current, expected_current);
        return -1;
    }
    if (memory_peak != expected_peak) {
        printf("Peak memory was %d instead of %d\n", memory_peak, expected_peak);
        return -1;
    }
    if (memory_total != expected_total) {
        printf("Total memory was %d instead of %d\n", memory_total, expected_total);
        return -1;
    }
    return 0;
}


// Return 0 if there is no error found
int check_error_parallel(int expected_current, int min_peak, int max_peak, int expected_total) {
    /*printf("Memory current: %d bytes, peak: %d bytes, total: %d bytes\n",
            memory_current, memory_peak, memory_total);*/
    if (memory_current != expected_current) {
        printf("Current memory was %d instead of %d\n", memory_current, expected_current);
        return -1;
    }
    if (memory_peak < min_peak || memory_peak > max_peak) {
        printf("Peak memory was %d which was outside the range of [%d, %d]\n", memory_peak, min_peak, max_peak);
        return -1;
    }
    if (memory_total != expected_total) {
        printf("Total memory was %d instead of %d\n", memory_total, expected_total);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment().with_feature(Target::Profile);

    Var x("x"), y("y");

    {
        printf("Running simple stack allocation test...\n");
        // On stack allocation (less than or equal to 1024*16 and buffer g1 size is constant 64x64)
        const int size_x = 64;
        const int size_y = 64;
        Func f1("f_1"), g1("g_1");
        g1(x, y) = x;
        f1(x, y) = g1(x%size_x, y%size_y);
        g1.compute_root();

        f1.set_custom_print(&my_print);
        //f1.print_loop_nest();

        reset_stats();
        f1.realize(size_x, size_y, t);
        if (check_error(0, 0, 0) != 0) {
            return -1;
        }
    }

    {
        printf("Running simple heap allocation test...\n");
        // On heap allocation (bigger than 1024*16)
        const int size_x = 1000;
        const int size_y = 1000;

        Func f2("f_2"), g2("g_2");
        g2(x, y) = x;
        f2(x, y) = g2(x-1, y) + g2(x, y-1);
        g2.compute_root();

        f2.set_custom_print(&my_print);
        //f2.print_loop_nest();

        reset_stats();
        f2.realize(size_x, size_y, t);
        int total = (size_x+1)*(size_y+1)*sizeof(int);
        if (check_error(0, total, total) != 0) {
            return -1;
        }
    }

    {
        printf("Running allocate condition is always false test...\n");
        // Allocate condiiton is always false
        Func f3("f_3"), g3("g_3");
        g3(x, y) = x*y;
        f3(x, y) = select(1 == 2, g3(x-1, y), 0);
        g3.compute_root();

        f3.set_custom_print(&my_print);
        //f3.print_loop_nest();

        reset_stats();
        f3.realize(1000, 1000, t);
        int total = 0;
        if (check_error(0, total, total) != 0) {
            return -1;
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

        f6.set_custom_print(&my_print);
        //f6.print_loop_nest();

        int total = 0;

        reset_stats();
        toggle1.set(true);
        toggle2.set(true);
        f6.realize(size_x, t);
        total = size_x*sizeof(float);
        if (check_error(0, total, total) != 0) {
            return -1;
        }

        reset_stats();
        toggle1.set(true);
        toggle2.set(false);
        f6.realize(size_x, t);
        total = size_x*sizeof(float);
        if (check_error(0, total, total) != 0) {
            return -1;
        }

        reset_stats();
        toggle1.set(false);
        toggle2.set(true);
        f6.realize(size_x, t);
        total = size_x*sizeof(float);
        if (check_error(0, total, total) != 0) {
            return -1;
        }

        reset_stats();
        toggle1.set(false);
        toggle2.set(false);
        f6.realize(size_x, t);
        total = 0;
        if (check_error(0, total, total) != 0) {
            return -1;
        }
    }

    {
        printf("Running allocate within loop test...\n");
        const int size_x = 1200;
        const int size_y = 1000;

        Func f7("f_7"), f8("f_8"), g5("g_5");
        g5(x, y) = x*y;
        f7(x, y) = g5(x, y);
        f8(x, y) = g5(x, y) + f7(x, y);

        g5.store_at(f8, y).compute_at(f8, y);
        f7.compute_at(f8, y);

        //f8.parallel(y);

        f8.set_custom_print(&my_print);
        //f8.print_loop_nest();

        reset_stats();
        f8.realize(size_x, size_y, t);
        int peak = size_x*sizeof(int);
        int total = size_x*size_y*sizeof(int);
        if (check_error(0, peak, total) != 0) {
            return -1;
        }
    }

    {
        printf("Running parallel allocate test...\n");
        const int size_x = 1200;
        const int size_y = 1000;

        Func f9("f_9"), f10("f_10"), g6("g_6");
        g6(x, y) = x*y;
        f9(x, y) = g6(x, y);
        f10(x, y) = g6(x, y) + f9(x, y);

        g6.store_at(f10, y).compute_at(f10, y);
        f9.compute_at(f10, y);

        f10.parallel(y);

        f10.set_custom_print(&my_print);
        //f10.print_loop_nest();

        reset_stats();
        f10.realize(size_x, size_y, t);
        int min_peak = size_x*sizeof(int);
        int total = size_x*size_y*sizeof(int);
        if (check_error_parallel(0, min_peak, total, total) != 0) {
            return -1;
        }
    }

    {
        printf("Running simple heap allocation test...\n");
        // On heap allocation (bigger than 1024*16 and buffer g7 size is constant 65x64)
        const int size_x = 65;
        const int size_y = 64;
        Func f11("f_11"), g7("g_7");
        g7(x, y) = x;
        f11(x, y) = g7(x%size_x, y%size_y);
        g7.compute_root();

        f11.set_custom_print(&my_print);
        //f11.print_loop_nest();

        reset_stats();
        f11.realize(size_x, size_y, t);
        int total = size_x*size_y*sizeof(int);
        if (check_error(0, total, total) != 0) {
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}

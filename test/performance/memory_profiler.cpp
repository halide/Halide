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

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment().with_feature(Target::Profile);

    Var x("x"), y("y");

    {
        printf("Running simple stack allocation test...\n");
        reset_stats();
        // On stack allocation (less than 1024*8)
        Func f("f_1"), g("g_1");
        g(x, y) = x;
        f(x, y) = g(x-1, y) + g(x, y-1);
        g.compute_root();

        f.set_custom_print(&my_print);
        Image<int> im = f.realize(10, 10, t);

        //f.print_loop_nest();

        if (check_error(0, 0, 0) != 0) {
            return -1;
        }
    }

    {
        printf("Running simple heap allocation test...\n");
        reset_stats();
        // On heap allocation (bigger than 1024*8)
        const int size_x = 1000;
        const int size_y = 1000;

        Func f("f_2"), g("g_2");
        g(x, y) = x;
        f(x, y) = g(x-1, y) + g(x, y-1);
        g.compute_root();

        f.set_custom_print(&my_print);
        Image<int> im = f.realize(size_x, size_y, t);

        //f.print_loop_nest();

        int total = (size_x+1)*(size_y+1)*sizeof(int);
        if (check_error(0, total, total) != 0) {
            return -1;
        }
    }

    {
        printf("Running allocate condition is always false test...\n");
        reset_stats();
        // Allocate condiiton is always false
        Func f("f_3"), g("g_3");
        g(x, y) = x*y;
        f(x, y) = select(1 == 2, g(x-1, y), 0);
        g.compute_root();

        f.set_custom_print(&my_print);
        Image<int> im = f.realize(1000, 1000, t);

        //f.print_loop_nest();

        int total = 0;
        if (check_error(0, total, total) != 0) {
            return -1;
        }
    }

    // TODO(psuriana): Add test for Allocate with non-trivial condition
    
    // TODO(psuriana): Add test for Allocate with non-trivial extent size (force
    // alloc on the heap)

    printf("Success!\n");
    return 0;
}

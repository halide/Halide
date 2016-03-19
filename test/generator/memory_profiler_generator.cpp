#include <math.h>
#include <stdio.h>
#include <assert.h>

#include "Halide.h"
#include "HalideRuntime.h"

using namespace Halide;

const int num_launcher_tasks = 1000;

struct ProfilerStats {
    ProfilerStats() : heap_peak(0), num_mallocs(0), malloc_avg(0), stack_peak(0) {}
    int heap_peak;
    int num_mallocs;
    int malloc_avg;
    int stack_peak;
};

static ProfilerStats profiler_stats[num_launcher_tasks];

void reset_stats(int index) {
    profiler_stats[index] = ProfilerStats();
}

void my_print(void *, const char *msg) {
    float this_ms;
    int idx, this_percentage, this_heap_peak;
    int this_num_mallocs, this_malloc_avg, this_stack_peak;
    int val;

    val = sscanf(msg, " g_%d: %fms (%d%%) peak: %d num: %d avg: %d",
        &idx, &this_ms, &this_percentage, &this_heap_peak,
        &this_num_mallocs, &this_malloc_avg);
    printf("%s\n", msg);
    if (val == 6) {
        profiler_stats[idx].heap_peak = this_heap_peak;
        profiler_stats[idx].num_mallocs = this_num_mallocs;
        profiler_stats[idx].malloc_avg = this_malloc_avg;
    }

    val = sscanf(msg, " g_%d: %fms (%d%%) stack: %d",
        &idx, &this_ms, &this_percentage, &this_stack_peak);
    printf("%s\n", msg);
    if (val == 4) {
        profiler_stats[idx].stack_peak = this_stack_peak;
    }
}

// Return 0 if there is no error found
int check_error_parallel(int index, int min_heap_peak, int max_heap_peak,
                         int exp_num_mallocs, int exp_malloc_avg, int exp_stack_peak) {
    /*printf("Memory heap_peak: %d bytes, num_mallocs: %d, malloc_avg: %d, "
            "stack_peak: %d\n", heap_peak, heap_total, num_mallocs,
            malloc_avg, stack_peak);*/
    if (profiler_stats[index].heap_peak < min_heap_peak || profiler_stats[index].heap_peak > max_heap_peak) {
        printf("Peak memory was %d which was outside the range of [%d, %d]\n",
               profiler_stats[index].heap_peak, min_heap_peak, max_heap_peak);
        return -1;
    }
    if (profiler_stats[index].num_mallocs != exp_num_mallocs) {
        printf("Num of mallocs was %d instead of %d\n", profiler_stats[index].num_mallocs, exp_num_mallocs);
        return -1;
    }
    if (profiler_stats[index].malloc_avg != exp_malloc_avg) {
        printf("Malloc average was %d instead of %d\n", profiler_stats[index].malloc_avg, exp_malloc_avg);
        return -1;
    }
    if (profiler_stats[index].stack_peak != exp_stack_peak) {
        printf("Stack peak was %d instead of %d\n", profiler_stats[index].stack_peak, exp_stack_peak);
        return -1;
    }
    return 0;
}

int launcher_task(void *user_context, int index, uint8_t *closure) {
    Target t = get_jit_target_from_environment().with_feature(Target::Profile);

    Var x("x"), y("y");

    // On stack allocation (less than or equal to 1024*16 and buffer g1 size is constant 64x64)
    const int size_x = 64;
    const int size_y = 64;
    std::string idx_str = std::to_string(index);
    Func f("f_" + index), g("g_" + index);
    g(x, y) = x;
    f(x, y) = g(x%size_x, y%size_y);
    g.compute_root();

    f.set_custom_print(&my_print);

    reset_stats(index);
    f.realize(size_x, size_y, t);
    int stack_size = size_x*size_y*sizeof(int);
    if (check_error_parallel(index, 0, 0, 0, 0, stack_size) != 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    // Hijack halide's runtime to run a bunch of instances of this function
    // in parallel.
    halide_do_par_for(nullptr, launcher_task, 0, num_launcher_tasks, nullptr);

    for (int i = 0; i < num_launcher_tasks; ++i) {
        //TODO
    }

    printf("Success!\n");
    return 0;
}

#include <assert.h>
#include <mutex>

#include "HalideRuntime.h"

#include "mandelbrot_generator.cpp"

using namespace Halide;

const int num_launcher_tasks = 2;

static std::mutex lock;

struct ProfilerStats {
    ProfilerStats() : heap_peak(0), num_mallocs(0), malloc_avg(0), stack_peak(0) {}
    int heap_peak;
    int num_mallocs;
    int malloc_avg;
    int stack_peak;
};

// Stats for the three functions in Mandelbrot: f (top), madelbrot, argmin in that order
static ProfilerStats profiler_stats[num_launcher_tasks][3];

void reset_all_stats() {
    std::lock_guard<std::mutex> lock_guard(lock);
    for (int i = 0; i < num_launcher_tasks; ++i) {
        for (int j = 0; j < 3; ++j) {
            profiler_stats[i][j] = ProfilerStats();
        }
    }
}

int get_toggle(const char *name) {
    if (strcmp(name, "f") == 0) {
        return 0;
    } else if (strcmp(name, "mandelbrot") == 0) {
        return 1;
    } else if (strcmp(name, "argmin")) {
        return 2;
    } else {
        assert(false);
    }
    return -1;
}

void my_print(void *, const char *msg) {
    float this_ms = 0;
    int idx = 0, this_percentage = 0, this_heap_peak = 0;
    int this_num_mallocs = 0, this_malloc_avg = 0, this_stack_peak = 0;
    int val = 0;
    char name[100];

    printf("%s\n", msg);

    val = sscanf(msg, " %s$%d: %fms (%d%%) peak: %d num: %d avg: %d",
        name, &idx, &this_ms, &this_percentage, &this_heap_peak,
        &this_num_mallocs, &this_malloc_avg);
    if (val == 7) {
        int toggle = get_toggle(name);
        std::lock_guard<std::mutex> lock_guard(lock);
        profiler_stats[idx][toggle].heap_peak = this_heap_peak;
        profiler_stats[idx][toggle].num_mallocs = this_num_mallocs;
        profiler_stats[idx][toggle].malloc_avg = this_malloc_avg;
    }
    printf("VAL: %d; name: %s\n", val, name);

    val = sscanf(msg, " %s$%d: %fms (%d%%) stack: %d",
        name, &idx, &this_ms, &this_percentage, &this_stack_peak);
    printf("VAL: %d; name: %s\n", val, name);
    if (val == 5) {
        int toggle = get_toggle(name);
        std::lock_guard<std::mutex> lock_guard(lock);
        profiler_stats[idx][toggle].stack_peak = this_stack_peak;
    }
}

// Return 0 if there is no error found
bool check_error_parallel(int index, int toggle, int min_heap_peak, int max_heap_peak,
                         int exp_num_mallocs, int exp_malloc_avg, int exp_stack_peak) {
    /*printf("Memory heap_peak: %d bytes, num_mallocs: %d, malloc_avg: %d, "
            "stack_peak: %d\n", profiler_stats[index][toggle].heap_peak, profiler_stats[index][toggle].num_mallocs,
            profiler_stats[index][toggle].malloc_avg, profiler_stats[index][toggle].stack_peak);*/
    std::lock_guard<std::mutex> lock_guard(lock);
    if (profiler_stats[index][toggle].heap_peak < min_heap_peak || profiler_stats[index][toggle].heap_peak > max_heap_peak) {
        printf("Peak memory was %d which was outside the range of [%d, %d]\n",
               profiler_stats[index][toggle].heap_peak, min_heap_peak, max_heap_peak);
        return false;
    }
    if (profiler_stats[index][toggle].num_mallocs != exp_num_mallocs) {
        printf("Num of mallocs was %d instead of %d\n", profiler_stats[index][toggle].num_mallocs, exp_num_mallocs);
        return false;
    }
    if (profiler_stats[index][toggle].malloc_avg != exp_malloc_avg) {
        printf("Malloc average was %d instead of %d\n", profiler_stats[index][toggle].malloc_avg, exp_malloc_avg);
        return false;
    }
    if (profiler_stats[index][toggle].stack_peak != exp_stack_peak) {
        printf("Stack peak was %d instead of %d\n", profiler_stats[index][toggle].stack_peak, exp_stack_peak);
        return false;
    }
    return true;
}

int launcher_mandelbrot_task(void *user_context, int index, uint8_t *closure) {
    const int width = 100;
    const int height = 30;
    const int iter = 20;

    float fx = cos(index / 10.0f), fy = sin(index / 10.0f);
    Mandelbrot gen;
    gen.set_generator_param_values({ { "target", "host-profile" } });
    gen.x_min.set(-2.0f);
    gen.x_max.set(2.0f);
    gen.y_min.set(-1.4f);
    gen.y_max.set(1.4f);
    gen.c_real.set(fx);
    gen.c_imag.set(fy);
    gen.iters.set(iter);
    gen.w.set(width);
    gen.h.set(height);

    Func f = gen.build();
    f.set_custom_print(&my_print);

    f.realize(width, height, gen.get_target());

    // Mandelbrot tiles by 8x8 and vectorizes x by 4
    const int tile_x = 8, tile_y = 8, vectorize = 4;
    // argmin (stack alloc)
    int stack_size = vectorize*sizeof(uint8_t) + vectorize*sizeof(int32_t);
    // mandelbrot (heap alloc)
    int y_niters = (height + tile_y - 1)/tile_y;
    int x_niters = (width + tile_x - 1)/tile_x;
    int heap_per_iter = 2*tile_x*tile_y*4*(iter+1);
    int n_mallocs = 2 * y_niters * x_niters;
    int heap_total = heap_per_iter * y_niters * x_niters;

    check_error_parallel(index, 0, 0, 0, 0, 0, 0);
    check_error_parallel(index, 1, heap_per_iter, heap_total, n_mallocs, heap_total/n_mallocs, 0);
    check_error_parallel(index, 3, 0, 0, 0, 0, stack_size);

    return 0;
}


int main(int argc, char **argv) {
    // Hijack halide's runtime to run a bunch of instances of this function
    // in parallel.
    reset_all_stats();
    halide_do_par_for(nullptr, launcher_mandelbrot_task, 0, num_launcher_tasks, nullptr);

    printf("Success!\n");
    return 0;
}

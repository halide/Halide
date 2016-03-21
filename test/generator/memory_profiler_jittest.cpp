#include <assert.h>
#include <mutex>

#include "HalideRuntime.h"

#include "mandelbrot_generator.cpp"
#include "memory_profiler_generator.cpp"
#include "tiled_blur_generator.cpp"

using namespace Halide;

const int num_launcher_tasks = 5;

static std::mutex lock;

struct ProfilerStats {
    ProfilerStats() : heap_peak(0), num_mallocs(0), malloc_avg(0), stack_peak(0) {}
    int heap_peak;
    int num_mallocs;
    int malloc_avg;
    int stack_peak;
};

static ProfilerStats profiler_stats[num_launcher_tasks];

void reset_all_stats() {
    std::lock_guard<std::mutex> lock_guard(lock);
    for (int i = 0; i < num_launcher_tasks; ++i) {
        profiler_stats[i] = ProfilerStats();
    }
}

void my_print(void *, const char *msg) {
    float this_ms;
    int idx, this_percentage, this_heap_peak;
    int this_num_mallocs, this_malloc_avg, this_stack_peak;
    int val;

    printf("%s\n", msg);

    val = sscanf(msg, " g_%d: %fms (%d%%) peak: %d num: %d avg: %d",
        &idx, &this_ms, &this_percentage, &this_heap_peak,
        &this_num_mallocs, &this_malloc_avg);
    if (val == 6) {
        std::lock_guard<std::mutex> lock_guard(lock);
        profiler_stats[idx].heap_peak = this_heap_peak;
        profiler_stats[idx].num_mallocs = this_num_mallocs;
        profiler_stats[idx].malloc_avg = this_malloc_avg;
    }

    val = sscanf(msg, " g_%d: %fms (%d%%) stack: %d",
        &idx, &this_ms, &this_percentage, &this_stack_peak);
    if (val == 4) {
        std::lock_guard<std::mutex> lock_guard(lock);
        profiler_stats[idx].stack_peak = this_stack_peak;
    }
}

// Return 0 if there is no error found
bool check_error_parallel(int index, int min_heap_peak, int max_heap_peak,
                         int exp_num_mallocs, int exp_malloc_avg, int exp_stack_peak) {
    /*printf("Memory heap_peak: %d bytes, num_mallocs: %d, malloc_avg: %d, "
            "stack_peak: %d\n", profiler_stats[index].heap_peak, profiler_stats[index].num_mallocs,
            profiler_stats[index].malloc_avg, profiler_stats[index].stack_peak);*/
    std::lock_guard<std::mutex> lock_guard(lock);
    if (profiler_stats[index].heap_peak < min_heap_peak || profiler_stats[index].heap_peak > max_heap_peak) {
        printf("Peak memory was %d which was outside the range of [%d, %d]\n",
               profiler_stats[index].heap_peak, min_heap_peak, max_heap_peak);
        return false;
    }
    if (profiler_stats[index].num_mallocs != exp_num_mallocs) {
        printf("Num of mallocs was %d instead of %d\n", profiler_stats[index].num_mallocs, exp_num_mallocs);
        return false;
    }
    if (profiler_stats[index].malloc_avg != exp_malloc_avg) {
        printf("Malloc average was %d instead of %d\n", profiler_stats[index].malloc_avg, exp_malloc_avg);
        return false;
    }
    if (profiler_stats[index].stack_peak != exp_stack_peak) {
        printf("Stack peak was %d instead of %d\n", profiler_stats[index].stack_peak, exp_stack_peak);
        return false;
    }
    return true;
}

int launcher_normal_task(void *user_context, int index, uint8_t *closure) {
    const int wrap_x = 64;
    const int wrap_y = 64;

    MemoryProfiler gen;
    gen.set_generator_param_values({ { "target", "host-profile" } });
    gen.index.set(index);
    gen.wrap_x.set(wrap_x);
    gen.wrap_y.set(wrap_y);

    Func f = gen.build();
    f.set_custom_print(&my_print);

    f.realize(1000, 1000, gen.get_target());

    int stack_size = wrap_x*wrap_y*sizeof(int);
    assert(check_error_parallel(index, 0, 0, 0, 0, stack_size));

    return 0;
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
    int y_niters = (height + tile_y - 1)/tile_y;
    int x_niters = (width + tile_x - 1)/tile_x;
    int stack_size = vectorize*sizeof(uint8_t) + vectorize*sizeof(int32_t);
    int heap_per_iter = 2*tile_x*tile_y*4*(iter+1);
    int n_mallocs = 2 * y_niters * x_niters;
    int heap_total = heap_per_iter * y_niters * x_niters;
    check_error_parallel(index, heap_per_iter, heap_total, n_mallocs, heap_total/n_mallocs, stack_size);

    return 0;
}


int launcher_tiled_blur_task(void *user_context, int index, uint8_t *closure) {
    const int width = 1000;
    const int height = 1300;

    Image<int> input(width, height, 3);
    for (int c = 0; c < 3; c++) {
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                input(x, y, c) = (x * y) + c;
            }
        }
    }

    TiledBlur gen;
    gen.set_generator_param_values({ { "target", "host-profile" } });
    gen.is_interleaved.set(true);
    gen.input.set(input);

    Func f = gen.build();
    f.set_custom_print(&my_print);

    f.realize(width, height, 3, gen.get_target());

    //int stack_size = width*height*sizeof(int);
    //assert(check_error_parallel(index, 0, 0, 0, 0, stack_size));

    return 0;
}

int main(int argc, char **argv) {
    // Hijack halide's runtime to run a bunch of instances of this function
    // in parallel.
    //reset_all_stats();
    //halide_do_par_for(nullptr, launcher_normal_task, 0, num_launcher_tasks, nullptr);

    reset_all_stats();
    halide_do_par_for(nullptr, launcher_mandelbrot_task, 0, num_launcher_tasks, nullptr);

    //reset_all_stats();
    //halide_do_par_for(nullptr, launcher_tiled_blur_task, 0, num_launcher_tasks, nullptr);

    printf("Success!\n");
    return 0;
}

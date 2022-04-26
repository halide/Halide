#include <assert.h>
#include <cmath>
#include <cstring>
#include <map>
#include <stdio.h>
#include <string>

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "memory_profiler_mandelbrot.h"

using namespace Halide::Runtime;

namespace {

using std::map;
using std::string;

const int num_launcher_tasks = 10000;

const int width = 100;
const int height = 30;
const int iters = 20;

// Mandelbrot tiles by 8x8 and vectorizes x by 4
const int tile_x = 8, tile_y = 8, vectorize = 4;

// Expected stack size for argmin
const int argmin_stack_peak = vectorize * sizeof(uint8_t) + vectorize * sizeof(int32_t);

// Expected heap size for mandelbrot
const int y_niters = (height + tile_y - 1) / tile_y;
const int x_niters = (width + tile_x - 1) / tile_x;
const int mandelbrot_n_mallocs = 2 * y_niters * x_niters * num_launcher_tasks;
const uint64_t mandelbrot_heap_per_iter = 2 * tile_x * tile_y * 4 * (iters + 1);  // Heap per iter for one task
const uint64_t mandelbrot_heap_total = mandelbrot_heap_per_iter * y_niters * x_niters * num_launcher_tasks;

void validate(halide_profiler_state *s) {
    for (halide_profiler_pipeline_stats *p = s->pipelines; p;
         p = (halide_profiler_pipeline_stats *)(p->next)) {
        assert(p->num_allocs == mandelbrot_n_mallocs);
        assert(p->memory_total == mandelbrot_heap_total);

        assert(mandelbrot_heap_per_iter <= p->memory_peak);
        assert(p->memory_peak <= mandelbrot_heap_total);

        for (int i = 0; i < p->num_funcs; i++) {
            halide_profiler_func_stats *fs = p->funcs + i;
            if (strncmp(fs->name, "argmin", 6) == 0) {
                assert(fs->stack_peak == argmin_stack_peak);
            } else if (strncmp(fs->name, "mandelbrot", 10) == 0) {
                assert(mandelbrot_heap_per_iter <= fs->memory_peak);
                assert(fs->memory_peak <= mandelbrot_heap_total);

                assert(fs->num_allocs == mandelbrot_n_mallocs);
                assert(fs->memory_total == mandelbrot_heap_total);
            }
        }
    }
}

int launcher_task(void *user_context, int index, uint8_t *closure) {
    Buffer<int, 2> output(width, height);
    float fx = cos(index / 10.0f), fy = sin(index / 10.0f);
    memory_profiler_mandelbrot(-2.0f, 2.0f, -1.4f, 1.4f, fx, fy, iters,
                               output.width(), output.height(), output);

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    // Hijack halide's runtime to run a bunch of instances of this function
    // in parallel.
    printf("Running memory profiler comparison test\n");
    printf("mandelbrot expected value\n  nmalocs (all tasks): %d, heap/iter "
           "(per task): %d K, heap total (all tasks): %d K\n",
           mandelbrot_n_mallocs,
           (int)(mandelbrot_heap_per_iter / 1024),
           (int)(mandelbrot_heap_total / 1024));
    printf("argmin expected value\n  stack peak: %d\n", argmin_stack_peak);
    printf("\n");

    // Note that launcher_task() always returns zero, thus halide_do_par_for()
    // should always return zero, but since this is a test, let's verify that.
    int result = halide_do_par_for(nullptr, launcher_task, 0, num_launcher_tasks, nullptr);
    assert(result == 0);
    (void)result;

    halide_profiler_state *state = halide_profiler_get_state();
    assert(state != nullptr);

    validate(state);

    printf("Success!\n");
    return 0;
}

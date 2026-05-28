#include "Halide.h"
#include "HalideRuntime.h"
#include <stdio.h>
#include <string.h>

using namespace Halide;

namespace {

// JITCache::finish_profiling calls halide_profiler_reset() right after
// the report fires, so we have to snapshot stats while the pipeline is
// still alive. halide_trace_end_pipeline fires inside the pipeline
// body — per-instance memory and stack counters are already populated
// by the time it fires; they just haven't been folded into
// pipeline_stats yet.
using GetStateFn = halide_profiler_state *(*)();
GetStateFn jit_profiler_get_state = nullptr;
Target jit_target;
const char *target_func_name = "";

int heap_peak = 0;
int num_mallocs = 0;
int malloc_avg = 0;
int stack_peak = 0;
bool captured = false;

int32_t snapshot_trace(JITUserContext *, const halide_trace_event_t *e) {
    if (captured || e->event != halide_trace_end_pipeline) {
        return 0;
    }
    if (!jit_profiler_get_state) {
        jit_profiler_get_state =
            (GetStateFn)Internal::JITSharedRuntime::find_symbol(jit_target, "halide_profiler_get_state");
        if (!jit_profiler_get_state) {
            return 0;
        }
    }
    captured = true;
    halide_profiler_instance_state *inst = jit_profiler_get_state()->instances;
    if (!inst) {
        return 0;
    }
    halide_profiler_pipeline_stats *p = inst->pipeline_stats;
    size_t target_len = strlen(target_func_name);
    for (int i = 0; i < p->num_funcs; i++) {
        const char *name = p->funcs[i].name;
        if (strncmp(name, target_func_name, target_len) != 0) {
            continue;
        }
        // Match exact or stage-qualified (e.g. "g_4" or "g_4.s0"), but not
        // a longer-named Func like "g_42".
        char next = name[target_len];
        if (next != '\0' && next != '.') {
            continue;
        }
        const halide_profiler_func_stats *fs = &inst->funcs[i];
        if (fs->num_allocs > 0) {
            heap_peak = (int)fs->memory_peak;
            num_mallocs = (int)fs->num_allocs;
            malloc_avg = (int)(fs->memory_total / fs->num_allocs);
        }
        if (fs->stack_peak > 0) {
            stack_peak = (int)fs->stack_peak;
        }
    }
    return 0;
}

void reset_stats() {
    heap_peak = 0;
    num_mallocs = 0;
    malloc_avg = 0;
    stack_peak = 0;
    captured = false;
}

void install_handlers(Pipeline &pipe, const Target &target, const char *name) {
    target_func_name = name;
    jit_target = target;
    pipe.trace_pipeline();
    pipe.jit_handlers().custom_trace = snapshot_trace;
}

int check_error(int exp_heap_peak, int exp_num_mallocs,
                int exp_malloc_avg, int exp_stack_peak) {
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

int check_error_parallel(int min_heap_peak, int max_heap_peak, int exp_num_mallocs,
                         int exp_malloc_avg, int exp_stack_peak) {
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

}  // namespace

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
        const int size_x = 64;
        const int size_y = 64;
        Func f1("f_1"), g1("g_1");
        g1(x, y) = x;
        f1(x, y) = g1(x % size_x, y % size_y);
        g1.compute_root();

        Pipeline pipe(f1);
        install_handlers(pipe, t, "g_1");

        reset_stats();
        pipe.realize({size_x, size_y}, t);
        int stack_size = size_x * size_y * sizeof(int);
        if (check_error(0, 0, 0, stack_size) != 0) {
            return 1;
        }
    }

    {
        printf("Running simple heap allocation test 1...\n");
        const int size_x = 1000;
        const int size_y = 1000;

        Func f2("f_2"), g2("g_2");
        g2(x, y) = x;
        f2(x, y) = g2(x - 1, y) + g2(x, y - 1);
        g2.compute_root();

        Pipeline pipe(f2);
        install_handlers(pipe, t, "g_2");

        reset_stats();
        pipe.realize({size_x, size_y}, t);
        int total = (size_x + 1) * (size_y + 1) * sizeof(int);
        if (check_error(total, 1, total, 0) != 0) {
            return 1;
        }
    }

    {
        printf("Running heap allocate condition is always false test...\n");
        Func f3("f_3"), g3("g_3");
        g3(x, y) = x * y;
        f3(x, y) = select(1 == 2, g3(x - 1, y), 0);
        g3.compute_root();

        Pipeline pipe(f3);
        install_handlers(pipe, t, "g_3");

        reset_stats();
        pipe.realize({1000, 1000}, t);
        if (check_error(0, 0, 0, 0) != 0) {
            return 1;
        }
    }

    {
        printf("Running stack allocate condition is always false test...\n");
        Func f3("f_3"), g3("g_3");
        g3(x, y) = x * y;
        f3(x, y) = select(1 == 2, g3((x - 1) % 10, y % 10), 0);
        g3.compute_root();

        Pipeline pipe(f3);
        install_handlers(pipe, t, "g_3");

        reset_stats();
        pipe.realize({1000, 1000}, t);
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

        Pipeline pipe(f6);
        install_handlers(pipe, t, "g_4");

        int total = 0;

        reset_stats();
        toggle1.set(true);
        toggle2.set(true);
        pipe.realize({size_x}, t);
        total = size_x * sizeof(float);
        if (check_error(total, 1, total, 0) != 0) {
            return 1;
        }

        reset_stats();
        toggle1.set(true);
        toggle2.set(false);
        pipe.realize({size_x}, t);
        total = size_x * sizeof(float);
        if (check_error(total, 1, total, 0) != 0) {
            return 1;
        }

        reset_stats();
        toggle1.set(false);
        toggle2.set(true);
        pipe.realize({size_x}, t);
        total = size_x * sizeof(float);
        if (check_error(total, 1, total, 0) != 0) {
            return 1;
        }

        reset_stats();
        toggle1.set(false);
        toggle2.set(false);
        pipe.realize({size_x}, t);
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

        Pipeline pipe(f8);
        install_handlers(pipe, t, "g_5");

        reset_stats();
        pipe.realize({size_x, size_y}, t);
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

        Pipeline pipe(f10);
        install_handlers(pipe, t, "g_6");

        reset_stats();
        pipe.realize({size_x, size_y}, t);
        int min_heap_peak = size_x * sizeof(int);
        int total = size_x * size_y * sizeof(int);
        if (check_error_parallel(min_heap_peak, total, size_y, total / size_y, 0) != 0) {
            return 1;
        }
    }

    {
        printf("Running simple heap allocation test 2...\n");
        const int size_x = 65;
        const int size_y = 64;
        Func f11("f_11"), g7("g_7");
        g7(x, y) = x;
        f11(x, y) = g7(x % size_x, y % size_y);
        g7.compute_root();

        Pipeline pipe(f11);
        install_handlers(pipe, t, "g_7");

        reset_stats();
        pipe.realize({size_x, size_y}, t);
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

        Pipeline pipe(f12);
        install_handlers(pipe, t, "g_8");

        reset_stats();
        pipe.realize({size_x, size_y}, t);
        int stack_size = size_x * size_y * sizeof(int);
        if (check_error(0, 0, 0, stack_size) != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}

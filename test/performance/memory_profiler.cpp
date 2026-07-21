#include "Halide.h"
#include "HalideRuntime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>

using namespace Halide;

// JITCache::finish_profiling calls halide_profiler_reset() right after
// the report fires, so we snapshot stats while the pipeline is still
// alive. halide_trace_end_pipeline fires inside the pipeline body —
// per-instance memory and stack counters are already populated by the
// time it fires; they just haven't been folded into pipeline_stats yet.
using GetStateFn = halide_profiler_state *(*)();
Target jit_target;
std::string target_func_name;

struct Stats {
    int heap_peak = 0;
    int num_mallocs = 0;
    int malloc_avg = 0;
    int stack_peak = 0;
};
Stats captured_stats;

int32_t snapshot_trace(JITUserContext *, const halide_trace_event_t *e) {
    if (e->event != halide_trace_end_pipeline) {
        return 0;
    }
    auto get_state = (GetStateFn)Internal::JITSharedRuntime::find_symbol(
        jit_target, "halide_profiler_get_state");
    if (!get_state) {
        return 0;
    }
    // Only one pipeline is running, so just grab the head.
    halide_profiler_instance_state *inst = get_state()->instances;
    if (!inst) {
        return 0;
    }
    halide_profiler_pipeline_stats *p = inst->pipeline_stats;
    for (int i = 0; i < p->num_funcs; i++) {
        if (std::string(p->funcs[i].name) != target_func_name) {
            continue;
        }
        const halide_profiler_func_stats *fs = &inst->funcs[i];
        if (fs->num_allocs > 0) {
            captured_stats.heap_peak = (int)fs->memory_peak;
            captured_stats.num_mallocs = (int)fs->num_allocs;
            captured_stats.malloc_avg = (int)(fs->memory_total / fs->num_allocs);
        }
        if (fs->stack_peak > 0) {
            captured_stats.stack_peak = (int)fs->stack_peak;
        }
        break;
    }
    return 0;
}

void install_handlers(Pipeline &pipe, const Func &target_func) {
    target_func_name = target_func.name();
    pipe.trace_pipeline();
    pipe.jit_handlers().custom_trace = snapshot_trace;
}

// Exits with code 1 if the captured stats don't match the expectations.
// exp_heap_peak is a range [min, max]; for an exact expectation pass it twice.
void check(int exp_heap_peak_min, int exp_heap_peak_max,
           int exp_num_mallocs, int exp_malloc_avg, int exp_stack_peak) {
    if (captured_stats.heap_peak < exp_heap_peak_min ||
        captured_stats.heap_peak > exp_heap_peak_max) {
        printf("Peak heap was %d, expected in [%d, %d]\n",
               captured_stats.heap_peak, exp_heap_peak_min, exp_heap_peak_max);
        exit(1);
    }
    if (captured_stats.num_mallocs != exp_num_mallocs) {
        printf("Num of mallocs was %d, expected %d\n",
               captured_stats.num_mallocs, exp_num_mallocs);
        exit(1);
    }
    if (captured_stats.malloc_avg != exp_malloc_avg) {
        printf("Malloc average was %d, expected %d\n",
               captured_stats.malloc_avg, exp_malloc_avg);
        exit(1);
    }
    if (captured_stats.stack_peak != exp_stack_peak) {
        printf("Stack peak was %d, expected %d\n",
               captured_stats.stack_peak, exp_stack_peak);
        exit(1);
    }
}

template<typename Fn>
void run_case(const char *desc, Fn body) {
    printf("Running %s...\n", desc);
    captured_stats = {};
    body();
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    jit_target = target.with_feature(Target::Profile);
    const Target &t = jit_target;

    Var x("x"), y("y");

    run_case("simple stack allocation test", [&]() {
        // On stack allocation (≤ 1024*16, and g1 is constant 64x64).
        const int size_x = 64, size_y = 64;
        Func f1("f_1"), g1("g_1");
        g1(x, y) = x;
        f1(x, y) = g1(x % size_x, y % size_y);
        g1.compute_root();

        Pipeline pipe(f1);
        install_handlers(pipe, g1);
        pipe.realize({size_x, size_y}, t);
        check(0, 0, 0, 0, size_x * size_y * (int)sizeof(int));
    });

    run_case("simple heap allocation test 1", [&]() {
        // On heap allocation (> 1024*16).
        const int size_x = 1000, size_y = 1000;
        Func f2("f_2"), g2("g_2");
        g2(x, y) = x;
        f2(x, y) = g2(x - 1, y) + g2(x, y - 1);
        g2.compute_root();

        Pipeline pipe(f2);
        install_handlers(pipe, g2);
        pipe.realize({size_x, size_y}, t);
        int total = (size_x + 1) * (size_y + 1) * (int)sizeof(int);
        check(total, total, 1, total, 0);
    });

    run_case("heap allocate condition is always false test", [&]() {
        Func f3("f_3"), g3("g_3");
        g3(x, y) = x * y;
        f3(x, y) = select(1 == 2, g3(x - 1, y), 0);
        g3.compute_root();

        Pipeline pipe(f3);
        install_handlers(pipe, g3);
        pipe.realize({1000, 1000}, t);
        check(0, 0, 0, 0, 0);
    });

    run_case("stack allocate condition is always false test", [&]() {
        Func f3("f_3"), g3("g_3");
        g3(x, y) = x * y;
        f3(x, y) = select(1 == 2, g3((x - 1) % 10, y % 10), 0);
        g3.compute_root();

        Pipeline pipe(f3);
        install_handlers(pipe, g3);
        pipe.realize({1000, 1000}, t);
        check(0, 0, 0, 0, 0);
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
        install_handlers(pipe, g4);

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
            captured_stats = {};
            toggle1.set(c.t1);
            toggle2.set(c.t2);
            pipe.realize({size_x}, t);
            check(c.exp_heap, c.exp_heap, c.exp_mallocs, c.exp_avg, 0);
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
        install_handlers(pipe, g5);
        pipe.realize({size_x, size_y}, t);

        int peak = size_x * (int)sizeof(int);
        int total = size_x * size_y * (int)sizeof(int);
        check(peak, peak, size_y, total / size_y, 0);
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
        install_handlers(pipe, g6);
        pipe.realize({size_x, size_y}, t);

        int min_heap = size_x * (int)sizeof(int);
        int total = size_x * size_y * (int)sizeof(int);
        check(min_heap, total, size_y, total / size_y, 0);
    });

    run_case("simple heap allocation test 2", [&]() {
        // On heap allocation (> 1024*16, g7 is constant 65x64).
        const int size_x = 65, size_y = 64;
        Func f11("f_11"), g7("g_7");
        g7(x, y) = x;
        f11(x, y) = g7(x % size_x, y % size_y);
        g7.compute_root();

        Pipeline pipe(f11);
        install_handlers(pipe, g7);
        pipe.realize({size_x, size_y}, t);
        int total = size_x * size_y * (int)sizeof(int);
        check(total, total, 1, total, 0);
    });

    run_case("parallel stack allocation test", [&]() {
        const int size_x = 10, size_y = 10;
        Func f12("f_12"), g8("g_8");
        g8(x, y) = x;
        f12(x, y) = g8(x % size_x, y % size_y);
        g8.store_at(f12, y).compute_at(f12, y);
        f12.parallel(y);

        Pipeline pipe(f12);
        install_handlers(pipe, g8);
        pipe.realize({size_x, size_y}, t);
        check(0, 0, 0, 0, size_x * size_y * (int)sizeof(int));
    });

    printf("Success!\n");
    return 0;
}

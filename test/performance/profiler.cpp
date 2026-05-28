#include "Halide.h"
#include "HalideRuntime.h"
#include <stdio.h>
#include <string.h>

using namespace Halide;

namespace {

// JITCache::finish_profiling calls halide_profiler_reset() right after
// every profiled run, so we have to snapshot stats while the pipeline
// is still alive. halide_trace_end_pipeline fires inside the pipeline
// body — by which point the sampler has populated the running
// instance's per-Func time counters, even though they haven't yet been
// folded into pipeline_stats.
using GetStateFn = halide_profiler_state *(*)();
GetStateFn jit_profiler_get_state = nullptr;
Target jit_target;
uint64_t fn13_time = 0;
uint64_t pipeline_time = 0;
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
    halide_profiler_state *s = jit_profiler_get_state();
    // Only one instance runs at a time in the JIT test, so just take the head.
    halide_profiler_instance_state *inst = s->instances;
    if (!inst) {
        return 0;
    }
    pipeline_time = inst->billed_time;
    halide_profiler_pipeline_stats *p = inst->pipeline_stats;
    for (int i = 0; i < p->num_funcs; i++) {
        if (strncmp(p->funcs[i].name, "fn13", 4) == 0) {
            fn13_time += inst->funcs[i].time;
        }
    }
    return 0;
}

}  // namespace

int run_test(bool use_timer_profiler) {
    // Make a long chain of finely-interleaved Funcs, of which one is very expensive.
    Func f[30];
    Var c, x;
    for (int i = 0; i < 30; i++) {
        f[i] = Func("fn" + std::to_string(i));
        if (i == 0) {
            f[i](c, x) = cast<float>(x + c);
        } else if (i == 13) {
            Expr e = f[i - 1](c, x);
            for (int j = 0; j < 200; j++) {
                e = sin(e);
            }
            f[i](c, x) = e;
        } else {
            f[i](c, x) = f[i - 1](c, x) * 2.0f;
        }
    }

    Func out("profiler_test_pipeline");
    out(c, x) = 0.0f;
    const int iters = 100;
    RDom r(0, iters);
    out(c, x) += r * f[29](c, x);

    out.compute_root();
    out.update().reorder(c, x, r);
    for (int i = 0; i < 30; i++) {
        f[i].compute_at(out, x);
    }

    Target target = get_jit_target_from_environment()
                        .with_feature(use_timer_profiler ? Target::ProfileByTimer : Target::Profile);

    Pipeline pipe(out);
    pipe.trace_pipeline();
    pipe.jit_handlers().custom_trace = snapshot_trace;

    jit_target = target;
    jit_profiler_get_state = nullptr;
    fn13_time = 0;
    pipeline_time = 0;
    captured = false;
    pipe.realize({10, 1000}, target);

    if (!captured) {
        printf("end_pipeline trace did not fire\n");
        return 1;
    }
    if (pipeline_time == 0) {
        printf("Pipeline had no billed time (samples didn't land)\n");
        return 1;
    }

    float ms = fn13_time / 1e6f;
    float percentage = 100.0f * fn13_time / (float)pipeline_time;
    printf("Time spent in fn13: %fms\n", ms);

    if (percentage < 40.0f) {
        printf("Percentage of runtime spent in f13: %.1f%%\n"
               "This is suspiciously low. It should be more like 66%%\n",
               percentage);
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

    printf("Testing thread based profiler.\n");
    int result = run_test(false);
    if (result != 0) {
        return 1;
    }
    if (get_jit_target_from_environment().os == Target::Linux) {
        printf("Testing timer based profiler.\n");
        result = run_test(true);
        if (result != 0) {
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}

#include "Halide.h"
#include "HalideRuntime.h"
#include <stdio.h>
#include <string>

using namespace Halide;

// JITCache::finish_profiling calls halide_profiler_reset() right after
// every profiled run, so we snapshot stats while the pipeline is still
// alive. halide_trace_end_pipeline fires inside the pipeline body —
// the sampler has populated the running instance's per-Func time
// counters by then, even though they haven't been folded into
// pipeline_stats yet.
using GetStateFn = halide_profiler_state *(*)();
Target jit_target;
std::string fn13_name;
uint64_t fn13_time = 0;
uint64_t pipeline_time = 0;

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
    pipeline_time = inst->billed_time;
    halide_profiler_pipeline_stats *p = inst->pipeline_stats;
    for (int i = 0; i < p->num_funcs; i++) {
        if (std::string(p->funcs[i].name) == fn13_name) {
            fn13_time = inst->funcs[i].time;
            break;
        }
    }
    return 0;
}

int run_test(bool use_timer_profiler) {
    // Funcs get unique names per process, so name them per-run to keep
    // the same string across the two run_test invocations.
    const char *suffix = use_timer_profiler ? "_timer" : "_thread";

    Func f[30];
    Var c, x;
    for (int i = 0; i < 30; i++) {
        f[i] = Func("fn" + std::to_string(i) + suffix);
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

    Func out(std::string("profiler_test_pipeline") + suffix);
    out(c, x) = 0.0f;
    RDom r(0, 100);
    out(c, x) += r * f[29](c, x);

    out.compute_root();
    out.update().reorder(c, x, r);
    for (int i = 0; i < 30; i++) {
        f[i].compute_at(out, x);
    }

    jit_target = get_jit_target_from_environment()
                     .with_feature(use_timer_profiler ? Target::ProfileByTimer : Target::Profile);
    fn13_name = f[13].name();
    fn13_time = 0;
    pipeline_time = 0;

    Pipeline pipe(out);
    pipe.trace_pipeline();
    pipe.jit_handlers().custom_trace = snapshot_trace;
    pipe.realize({10, 1000}, jit_target);

    if (pipeline_time == 0) {
        printf("Pipeline had no billed time (samples didn't land)\n");
        return 1;
    }

    float percentage = 100.0f * fn13_time / (float)pipeline_time;
    printf("Time spent in fn13: %fms\n", fn13_time / 1e6f);

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
    if (run_test(false) != 0) {
        return 1;
    }
    if (target.os == Target::Linux) {
        printf("Testing timer based profiler.\n");
        if (run_test(true) != 0) {
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}

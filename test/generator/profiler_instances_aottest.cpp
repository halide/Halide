#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "profiler_instances.h"

using namespace Halide::Runtime;

// Extern stage used by the profiler_instances generator's
// extern_stage_e Func. Its only Expr arg is the inlined value of
// extern_inlined(2) (i.e. 2*7+3 = 17 after simplification, but
// arriving here wrapped in an inline_marker chain in the IR pre-
// lowering — the test asserts the marker survives correctly).
extern "C" int test_extern_stage(int seed, halide_buffer_t *out) {
    if (out->is_bounds_query()) {
        return 0;
    }
    int *p = (int *)out->host;
    for (int i = 0; i < out->dim[0].extent; i++) {
        p[i] = seed + i;
    }
    return 0;
}

namespace {

void fail(const char *what) {
    fprintf(stderr, "FAIL: %s\n", what);
    exit(1);
}

#define REQUIRE(cond)                          \
    do {                                       \
        if (!(cond)) {                         \
            fail("REQUIRE(" #cond ") failed"); \
        }                                      \
    } while (0)

// Find every entry (row in the per-Func stats array) with the given name.
std::vector<const halide_profiler_func_stats *> entries_of(
    const halide_profiler_pipeline_stats *p, const char *name) {
    std::vector<const halide_profiler_func_stats *> out;
    for (int i = 0; i < p->num_funcs; i++) {
        if (!strcmp(p->funcs[i].name, name)) {
            out.push_back(p->funcs + i);
        }
    }
    return out;
}

const char *parent_name(const halide_profiler_pipeline_stats *p,
                        const halide_profiler_func_stats *fs) {
    if (fs->parent < 0) {
        return "(root)";
    }
    return p->funcs[fs->parent].name;
}

int parent_id_of(const halide_profiler_pipeline_stats *p, const char *name) {
    auto xs = entries_of(p, name);
    REQUIRE(xs.size() == 1);
    return (int)(xs[0] - p->funcs);
}

// Assertions on the profiler state. -----------------------------------------

void check_two_compute_root_callers(const halide_profiler_pipeline_stats *p) {
    auto g = entries_of(p, "caller_g");
    auto h = entries_of(p, "caller_h");
    REQUIRE(g.size() == 1);
    REQUIRE(h.size() == 1);
    // Each is its own canonical (only one entry per name).
    REQUIRE(g[0]->canonical_id == (int)(g[0] - p->funcs));
    REQUIRE(h[0]->canonical_id == (int)(h[0] - p->funcs));
}

void check_unscheduled_update_multiple_entries(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "update_f");
    REQUIRE(fs.size() == 2);
    REQUIRE(fs[0]->canonical_id == fs[1]->canonical_id);
}

// GPU-only: an outer CPU loop with a host-then-device-then-host data
// chain forces explicit halide_copy_to_host / halide_copy_to_device calls
// to fire once per outer iteration. The synthetic copy "Func" entries
// should be parented somewhere inside the xfer_out producer tree (rather
// than at the pipeline root).
void check_copy_synthetics_parented_to_producer(const halide_profiler_pipeline_stats *p) {
    auto xfer_out = entries_of(p, "xfer_out");
    REQUIRE(xfer_out.size() == 1);
    int xfer_out_id = (int)(xfer_out[0] - p->funcs);

    auto descends_from = [&](int idx, int ancestor_id) {
        while (idx >= 0) {
            if (idx == ancestor_id) {
                return true;
            }
            idx = p->funcs[idx].parent;
        }
        return false;
    };

    auto check = [&](const char *copy_name) {
        auto fs = entries_of(p, copy_name);
        REQUIRE(fs.size() == 1);
        int idx = (int)(fs[0] - p->funcs);
        REQUIRE(p->funcs[idx].parent != -1);
        REQUIRE(descends_from(p->funcs[idx].parent, xfer_out_id));
    };
    check("xfer_dev (copy to host)");
    check("xfer_host (copy to device)");
}

// GPU-only: when a Func has multiple update defs and some are scheduled
// on host while others are on device, IHDBC injects copy synthetics
// inside the Func's own producer.
void check_mixed_host_device_update_defs(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "mixed_sched");
    REQUIRE(fs.size() == 1);
    int mixed_id = (int)(fs[0] - p->funcs);

    bool found_mid_func_copy = false;
    for (int i = 0; i < p->num_funcs; i++) {
        const halide_profiler_func_stats *child = p->funcs + i;
        if (child->parent != mixed_id) {
            continue;
        }
        if (strncmp(child->name, "mixed_sched ", strlen("mixed_sched ")) == 0 &&
            (strstr(child->name, " (copy to host)") || strstr(child->name, " (copy to device)"))) {
            found_mid_func_copy = true;
        }
    }
    REQUIRE(found_mid_func_copy);
}

}  // namespace

int main(int argc, char **argv) {
    // Size deliberately not a multiple of the RoundUp/GuardWithIf split
    // factor (10), so the tail strategies actually do something.
    Buffer<int, 1> output(73);
    // stride=1 is hidden from the compiler — the slide_fail_g scenario
    // calls g(stride*x) and g(stride*x + 3), and since the sign of stride
    // is unknown at compile time, sliding-window monotonicity fails.
    profiler_instances(1, output);

    halide_profiler_state *s = halide_profiler_get_state();
    REQUIRE(s != nullptr);
    const halide_profiler_pipeline_stats *target = nullptr;
    for (auto *p = s->pipelines; p; p = (halide_profiler_pipeline_stats *)p->next) {
        if (!strcmp(p->name, "profiler_instances")) {
            target = p;
            break;
        }
    }
    REQUIRE(target != nullptr);

    check_two_compute_root_callers(target);
    check_unscheduled_update_multiple_entries(target);
    // Only present when the pipeline was built with a GPU feature — the
    // generator gates the corresponding Funcs on get_target().has_gpu_feature().
    if (!entries_of(target, "xfer_out").empty()) {
        check_copy_synthetics_parented_to_producer(target);
    }
    if (!entries_of(target, "mixed_sched").empty()) {
        check_mixed_host_device_update_defs(target);
    }

    printf("Success!\n");
    return 0;
}

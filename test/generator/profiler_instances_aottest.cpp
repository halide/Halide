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

// RoundUp tail strategy on a compute_root Func over-computes the tail
// of the loop. The realize-box counter (points_required_at_realization)
// reflects the requested extent and misses this; points_computed
// (pure-def stage-0 stores × lanes) catches it. So we expect
// points_computed to exceed points_required_at_root.
void check_roundup_overstores_bytes(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "roundup_outer");
    REQUIRE(fs.size() == 1);
    REQUIRE(fs[0]->points_required_at_root > 0);
    REQUIRE(fs[0]->points_computed > fs[0]->points_required_at_root);
}

// GuardWithIf tail strategy: the tail iterations are guarded, so no
// extra stores actually happen. points_computed matches the required
// extent.
void check_guardwithif_no_overstore(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "guard_outer");
    REQUIRE(fs.size() == 1);
    REQUIRE(fs[0]->points_required_at_root > 0);
    REQUIRE(fs[0]->points_computed == fs[0]->points_required_at_root);
}

// Fully-unrolled Func with both a pure def and an update def. There are
// no stage-naming For loops in the IR — only the declare_stage marker
// from ScheduleFunctions can distinguish pure-def stores from update-
// def stores. points_computed should count exactly the pure-def
// stores (4 of them, one per unrolled iteration), not the update-def
// stores.
void check_unrolled_pure_update(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "unrolled_pu");
    REQUIRE(fs.size() == 1);
    REQUIRE(fs[0]->points_computed == 4);
}

// compute_with: two Funcs share a loop nest, so their stage stores
// appear interleaved in the IR. The per-Func pure-def tracking has to
// attribute each Store to the right Func. compute_with also gives each
// Func multiple entries (one for the actual producer plus
// box-required artifacts under the fused partner), so we sum across
// entries. Each Func is pure-only and gets stored at every output
// point exactly once, so its points_computed should equal its
// points_required_at_root.
void check_compute_with(const halide_profiler_pipeline_stats *p) {
    auto a = entries_of(p, "cw_a");
    auto b = entries_of(p, "cw_b");
    REQUIRE(!a.empty());
    REQUIRE(!b.empty());
    uint64_t a_computed = 0;
    uint64_t b_computed = 0;
    for (auto *fs : a) {
        a_computed += fs->points_computed;
    }
    for (auto *fs : b) {
        b_computed += fs->points_computed;
    }
    int a_canon = a[0]->canonical_id;
    int b_canon = b[0]->canonical_id;
    REQUIRE(a_computed > 0);
    REQUIRE(b_computed > 0);
    REQUIRE(a_computed == p->funcs[a_canon].points_required_at_root);
    REQUIRE(b_computed == p->funcs[b_canon].points_required_at_root);
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

// tab is an inlined Func whose root box is `ux * ux` for
// ux = cast<int32>(cast<uint16>(x)) — bounds inference can't prove the
// product fits in int32 ([0, 65535] * [0, 65535] = up to 4_294_836_225,
// which overflows), so simplify materialises a signed_integer_overflow
// intrinsic inside the declare_box_required_at_root marker for tab.
// (compute_root'ing tab with this same index expression makes the same
// intrinsic reach codegen and user_errors.) Without the poison-drop
// pre-pass in inject_profiling that marker reaches codegen and breaks
// the compile; with the pre-pass the marker is silently dropped, the
// pipeline compiles, and tab's points_required_at_root counter stays at
// zero (we lose the root-box count for the poisoned chain but
// everything else still works). tab_caller, the inlined wrapper that
// consumes tab, still has a well-defined root box of its own.
void check_points_required_at_root_canonical_only(const halide_profiler_pipeline_stats *p) {
    // For any Func with multiple entries, at most one entry should have a
    // non-zero points_required_at_root (the canonical one — that's where the
    // compiler bills the pipeline-wide root box).
    int entries_with_pr_at_root = 0;
    int total_multi_entry_funcs = 0;
    auto check = [&](const char *name) {
        auto xs = entries_of(p, name);
        if (xs.size() <= 1) {
            return;
        }
        total_multi_entry_funcs++;
        int with_pr = 0;
        int canon = xs[0]->canonical_id;
        for (auto *fs : xs) {
            if (fs->points_required_at_root > 0) {
                with_pr++;
                REQUIRE((int)(fs - p->funcs) == canon);
            }
        }
        REQUIRE(with_pr <= 1);
        entries_with_pr_at_root += with_pr;
    };
    check("update_f");
    REQUIRE(total_multi_entry_funcs >= 1);
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
    check_roundup_overstores_bytes(target);
    check_guardwithif_no_overstore(target);
    check_unrolled_pure_update(target);
    check_compute_with(target);
    // Only present when the pipeline was built with a GPU feature — the
    // generator gates the corresponding Funcs on get_target().has_gpu_feature().
    if (!entries_of(target, "xfer_out").empty()) {
        check_copy_synthetics_parented_to_producer(target);
    }
    if (!entries_of(target, "mixed_sched").empty()) {
        check_mixed_host_device_update_defs(target);
    }
    check_points_required_at_root_canonical_only(target);

    printf("Success!\n");
    return 0;
}

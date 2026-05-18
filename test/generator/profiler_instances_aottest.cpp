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

#define REQUIRE(cond)                              \
    do {                                           \
        if (!(cond)) {                             \
            fail("REQUIRE(" #cond ") failed");     \
        }                                          \
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
    REQUIRE(g[0]->productions > 0);
    REQUIRE(h[0]->productions > 0);
    // Each is its own canonical (only one entry per name).
    REQUIRE(g[0]->canonical_id == (int)(g[0] - p->funcs));
    REQUIRE(h[0]->canonical_id == (int)(h[0] - p->funcs));
}

void check_inlined_func_multiple_callers(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "multi_inlined");
    REQUIRE(fs.size() == 2);
    REQUIRE(fs[0]->canonical_id == fs[1]->canonical_id);

    int g_idx = parent_id_of(p, "caller_g");
    int h_idx = parent_id_of(p, "caller_h");
    bool parents_ok = (fs[0]->parent == g_idx && fs[1]->parent == h_idx) ||
                      (fs[0]->parent == h_idx && fs[1]->parent == g_idx);
    REQUIRE(parents_ok);
    REQUIRE(fs[0]->inlined_calls > 0);
    REQUIRE(fs[1]->inlined_calls > 0);
}

void check_inlining_chain(const halide_profiler_pipeline_stats *p) {
    // a -> b -> c -> caller_{g,h}: each link must point at the next-out one.
    auto a = entries_of(p, "chain_a");
    auto b = entries_of(p, "chain_b");
    auto c = entries_of(p, "chain_c");
    REQUIRE(a.size() == 2);
    REQUIRE(b.size() == 2);
    REQUIRE(c.size() == 2);
    for (auto *fs : a) {
        REQUIRE(strcmp(parent_name(p, fs), "chain_b") == 0);
    }
    for (auto *fs : b) {
        REQUIRE(strcmp(parent_name(p, fs), "chain_c") == 0);
    }
    for (auto *fs : c) {
        const char *pn = parent_name(p, fs);
        REQUIRE(strcmp(pn, "caller_g") == 0 || strcmp(pn, "caller_h") == 0);
    }
    // All entries of the same name share a canonical_id.
    REQUIRE(a[0]->canonical_id == a[1]->canonical_id);
    REQUIRE(b[0]->canonical_id == b[1]->canonical_id);
    REQUIRE(c[0]->canonical_id == c[1]->canonical_id);
    // Each link has billed inlined calls in every entry.
    for (auto *fs : a) {
        REQUIRE(fs->inlined_calls > 0);
    }
    for (auto *fs : b) {
        REQUIRE(fs->inlined_calls > 0);
    }
    for (auto *fs : c) {
        REQUIRE(fs->inlined_calls > 0);
    }
}

void check_unscheduled_update_multiple_entries(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "update_f");
    REQUIRE(fs.size() == 2);
    REQUIRE(fs[0]->canonical_id == fs[1]->canonical_id);
    REQUIRE(fs[0]->realizations > 0);
    REQUIRE(fs[1]->realizations > 0);
    REQUIRE(fs[0]->productions > 0);
    REQUIRE(fs[1]->productions > 0);
    // A Func with update definitions can't actually be inlined as code —
    // it needs scratch storage across stages — so no matter how it's
    // "unscheduled", its inlined_calls counter must stay at zero. It
    // shows up as a producer (realizations/productions) instead.
    REQUIRE(fs[0]->inlined_calls == 0);
    REQUIRE(fs[1]->inlined_calls == 0);
}

void check_cse_hoisted_marker(const halide_profiler_pipeline_stats *p) {
    // cse_shared appears in both the arg and the value of cse_user's update
    // Provide; Inline.cpp's CSE will hoist the shared computation into a
    // LetStmt above the Provide. The post-pass has to strip that.
    auto fs = entries_of(p, "cse_shared");
    REQUIRE(!fs.empty());
    uint64_t total = 0;
    for (auto *inst : fs) {
        total += inst->inlined_calls;
    }
    REQUIRE(total > 0);
}

// Diamond inlining: diamond_shared is reached from diamond_outer via two
// different intermediate markers (diamond_left and diamond_right). CSE
// factors diamond_shared's marker into a let above the Provide so its
// entry has two parents in the inlining graph. The LCA computation
// in PreAllocateEntries should attribute it to diamond_outer (the
// deepest node both diamond_left and diamond_right share), not to the
// Provide root or to whichever edge happened to be processed last.
void check_diamond_lca_attribution(const halide_profiler_pipeline_stats *p) {
    auto outer = entries_of(p, "diamond_outer");
    auto shared = entries_of(p, "diamond_shared");
    REQUIRE(!outer.empty());
    REQUIRE(!shared.empty());
    int outer_idx = (int)(outer[0] - p->funcs);
    for (auto *fs : shared) {
        REQUIRE(fs->parent == outer_idx);
    }
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
    // Total stores = pure-def + update-def = 8.
    uint64_t total_stores = fs[0]->scalar_stores + fs[0]->vector_stores + fs[0]->scatters;
    REQUIRE(total_stores == 8);
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

// compute_at of a stencil producer into tiles of the consumer: each tile
// loads a slightly wider box (halo) of the producer, so tile-boundary
// points are computed in more than one tile. Recompute is modest — > 1
// but well under 2.
void check_tiled_stencil_modest_recompute(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "stencil_p");
    REQUIRE(fs.size() == 1);
    REQUIRE(fs[0]->points_required_at_root > 0);
    REQUIRE(fs[0]->points_required_at_realization > fs[0]->points_required_at_root);
    REQUIRE(fs[0]->points_required_at_realization < 2 * fs[0]->points_required_at_root);
}

// Forced-inline Func — `forced_inline` is called with a data-dependent
// index so Halide can't bound it; it has to be inlined. The interesting
// thing being tested here is that the inline_marker for it survives the
// post-pass cleanly (rather than leaking to codegen), gets a slot in the
// stats array, and bills inlined calls. We also assert that
// points_required_at_root is zero on every entry: BoundsInference
// can't compute a finite root box for an unbounded Func, so it doesn't
// emit declare_box_required_root for it and the canonical entry's
// counter stays at zero (no recompute ratio is meaningful).
void check_forced_inline(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "forced_inline");
    REQUIRE(!fs.empty());
    uint64_t total = 0;
    for (auto *inst : fs) {
        total += inst->inlined_calls;
        REQUIRE(inst->points_required_at_root == 0);
    }
    REQUIRE(total > 0);
}

// Sliding window: stencil producer with compute_at xi, store_at xo. With
// sliding, each produce-firing past the warmup computes just the leading
// edge of the stencil. Summed per produce-firing per outer tile that
// equals the realize-box per outer tile (the strips tile the stored
// box). Both should be substantially less than num_inner_iters * 3
// (which is what production would be without sliding), and both should
// exceed points_required_at_root only by the per-tile halo overhead.
void check_sliding_window_counters(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "slide_p");
    REQUIRE(fs.size() == 1);
    uint64_t at_real = fs[0]->points_required_at_realization;
    uint64_t at_prod = fs[0]->points_required_at_production;
    uint64_t at_root = fs[0]->points_required_at_root;
    REQUIRE(at_root > 0);
    REQUIRE(at_real > 0);
    REQUIRE(at_prod > 0);
    // Sliding pulled the per-production cost in line with the realize
    // box (both sum the stored stripe per outer tile).
    REQUIRE(at_prod == at_real);
    // Both exceed the root box only by per-tile halo (small fraction).
    REQUIRE(at_real >= at_root);
    REQUIRE(at_real < 2 * at_root);
}

// Sliding-window FAILURE diagnostic: when the producer is called with a
// data-dependent stride that bounds analysis can't sign-bound, sliding's
// monotonicity check fails. The producer fires once per inner iter and
// recomputes its full footprint each time. points_required_at_production
// summed per produce-firing therefore substantially exceeds the realize
// box (which is just the stored stripe per outer tile). Comparing the
// two counters surfaces the slide failure.
void check_sliding_window_failure_counters(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "slide_fail_g");
    REQUIRE(fs.size() == 1);
    uint64_t at_real = fs[0]->points_required_at_realization;
    uint64_t at_prod = fs[0]->points_required_at_production;
    REQUIRE(at_real > 0);
    REQUIRE(at_prod > 0);
    // The smoking gun: production summed per produce-firing is much
    // larger than the realization-time storage box per outer tile.
    REQUIRE(at_prod > at_real);
}

// GPU-only: RDom::where with a Func-load predicate becomes an IfThenElse
// inside the update body whose condition is impure. Inside a GPU kernel,
// InjectCounters can't flush mid-kernel, so it hoists the depending
// counter contributions via max-of-branches and flags the entry's
// stats as approximated.
void check_counters_approximated_on_impure_if(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "approx_out");
    REQUIRE(!fs.empty());
    bool any_approx = false;
    for (auto *inst : fs) {
        any_approx = any_approx || inst->counters_approximated;
    }
    REQUIRE(any_approx);
}

// GPU-only: an outer CPU loop with a host-then-device-then-host data
// chain forces explicit halide_copy_to_host / halide_copy_to_device calls
// to fire once per outer iteration. The synthetic copy "Func" entries
// should be parented somewhere inside the xfer_out producer tree (rather
// than at the pipeline root), and their realizations counter should
// equal the outer-loop trip count.
void check_copy_synthetics_parented_to_producer(const halide_profiler_pipeline_stats *p) {
    auto xfer_out = entries_of(p, "xfer_out");
    REQUIRE(xfer_out.size() == 1);
    int xfer_out_id = (int)(xfer_out[0] - p->funcs);

    // Walk up the parent chain and check whether `ancestor_id` is in it.
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
        // The synthetic copy "Func" must thread into the timeline under
        // xfer_out (the producer that contains the outer CPU loop), not
        // appear as a root-level entry. Its immediate parent may be the
        // consumer (host or device producer) that needed the data, which
        // is itself nested under xfer_out.
        int idx = (int)(fs[0] - p->funcs);
        REQUIRE(p->funcs[idx].parent != -1);
        REQUIRE(descends_from(p->funcs[idx].parent, xfer_out_id));
        // It must have fired more than once (the outer loop has multiple
        // iterations). The counter is bumped exactly once per copy.
        REQUIRE(fs[0]->realizations > 1);
    };
    check("xfer_dev (copy to host)");
    check("xfer_host (copy to device)");
}

// GPU-only: when a Func has multiple update defs and some are scheduled
// on host while others are on device, IHDBC injects copy synthetics
// inside the Func's own producer. The "Func has stages on different
// devices" rule should fire (signaled by the parent-of-copy having the
// same base name as the copy buffer).
void check_mixed_host_device_update_defs(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "mixed_sched");
    REQUIRE(fs.size() == 1);
    int mixed_id = (int)(fs[0] - p->funcs);

    // Find at least one copy synthetic that's a direct child of
    // mixed_sched whose name starts with "mixed_sched ".
    bool found_mid_func_copy = false;
    for (int i = 0; i < p->num_funcs; i++) {
        const halide_profiler_func_stats *child = p->funcs + i;
        if (child->parent != mixed_id) {
            continue;
        }
        if (strncmp(child->name, "mixed_sched ", strlen("mixed_sched ")) == 0 &&
            (strstr(child->name, " (copy to host)") || strstr(child->name, " (copy to device)"))) {
            found_mid_func_copy = true;
            // Counter should reflect the copy firing at least once.
            REQUIRE(child->realizations >= 1);
        }
    }
    REQUIRE(found_mid_func_copy);
}

// Extern stage with an inlined Func in its scalar arg: the inlined entry
// should be parented to the extern stage (not to whatever surrounds it),
// confirming the extern_stage_marker path in resolve_inline_markers
// correctly anchors inline_markers buried in extern call args.
void check_extern_stage_inlined_parent(const halide_profiler_pipeline_stats *p) {
    auto ei = entries_of(p, "extern_inlined");
    REQUIRE(!ei.empty());
    int ese_id = parent_id_of(p, "extern_stage_e");
    for (auto *fs : ei) {
        REQUIRE(fs->parent == ese_id);
        REQUIRE(fs->inlined_calls > 0);
    }
}

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
    check("multi_inlined");
    check("chain_a");
    check("chain_b");
    check("chain_c");
    check("update_f");
    REQUIRE(total_multi_entry_funcs >= 3);
}

}  // namespace

int main(int argc, char **argv) {
    // Size deliberately not a multiple of the RoundUp/GuardWithIf split
    // factor (10), so the tail strategies actually do something.
    Buffer<int, 1> output(73);
    // table16 feeds the wide-bounds scenario. Contents don't matter for the
    // assertion; we just need a real buffer.
    Buffer<uint16_t, 1> table16(1024);
    table16.fill(0);
    // stride=1 is hidden from the compiler — the slide_fail_g scenario
    // calls g(stride*x) and g(stride*x + 3), and since the sign of stride
    // is unknown at compile time, sliding-window monotonicity fails.
    profiler_instances(1, table16, output);

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
    check_inlined_func_multiple_callers(target);
    check_inlining_chain(target);
    check_unscheduled_update_multiple_entries(target);
    check_cse_hoisted_marker(target);
    check_diamond_lca_attribution(target);
    check_forced_inline(target);
    check_roundup_overstores_bytes(target);
    check_guardwithif_no_overstore(target);
    check_unrolled_pure_update(target);
    check_compute_with(target);
    check_tiled_stencil_modest_recompute(target);
    check_sliding_window_counters(target);
    check_sliding_window_failure_counters(target);
    check_extern_stage_inlined_parent(target);
    // Only present when the pipeline was built with a GPU feature — the
    // generator gates the corresponding Funcs on get_target().has_gpu_feature().
    if (!entries_of(target, "approx_out").empty()) {
        check_counters_approximated_on_impure_if(target);
    }
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

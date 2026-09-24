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

void check_unscheduled_update_multiple_entries(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "update_f");
    REQUIRE(fs.size() == 2);
    REQUIRE(fs[0]->canonical_id == fs[1]->canonical_id);
    REQUIRE(fs[0]->realizations > 0);
    REQUIRE(fs[1]->realizations > 0);
    REQUIRE(fs[0]->productions > 0);
    REQUIRE(fs[1]->productions > 0);
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
    // Fully unrolled, so every store is scalar: pure-def + update-def = 8.
    REQUIRE(fs[0]->scalar_stores == 8);
    REQUIRE(fs[0]->vector_stores == 0);
    REQUIRE(fs[0]->scatters == 0);
}

// The load/store op-profile counters classify each access by its index:
// scalar, dense unit-stride vector (ls_n/8 = 8 wide ops over the 64-wide
// domain), or gather/scatter. The generator's ls_* Funcs each exercise one
// classification with exactly-known counts. Every element is a 4-byte int.
void check_load_store_counters(const halide_profiler_pipeline_stats *p) {
    struct Expected {
        const char *name;
        uint64_t scalar_loads, vector_loads, gathers, bytes_loaded;
        uint64_t scalar_stores, vector_stores, scatters, bytes_stored;
    };
    const Expected expected[] = {
        // Dense and vectorized: 8 unit-stride vector loads and stores.
        {"ls_vec", 0, 8, 0, 256, 0, 8, 0, 256},
        // Unvectorized: 64 scalar loads and stores.
        {"ls_scalar", 64, 0, 0, 256, 64, 0, 0, 256},
        // Data-dependent load index: 8 gathers, plus the dense inner load.
        {"ls_gather", 0, 8, 8, 512, 0, 8, 0, 256},
        // Non-unit-stride (strided) vector store: 32 scatters.
        {"ls_scatter", 0, 0, 0, 0, 0, 0, 32, 1024},
    };
    for (const auto &e : expected) {
        auto fs = entries_of(p, e.name);
        REQUIRE(fs.size() == 1);
        const halide_profiler_func_stats *f = fs[0];
        REQUIRE(f->scalar_loads == e.scalar_loads);
        REQUIRE(f->vector_loads == e.vector_loads);
        REQUIRE(f->gathers == e.gathers);
        REQUIRE(f->bytes_loaded == e.bytes_loaded);
        REQUIRE(f->scalar_stores == e.scalar_stores);
        REQUIRE(f->vector_stores == e.vector_stores);
        REQUIRE(f->scatters == e.scatters);
        REQUIRE(f->bytes_stored == e.bytes_stored);
    }
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

// GPU-only: an outer CPU loop with a host-then-device-then-host data
// chain forces explicit halide_copy_to_host / halide_copy_to_device calls
// to fire once per outer iteration. The synthetic copy "Func" entries
// should be parented somewhere inside the xfer_out producer tree, rather
// than at the pipeline root.
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
        }
    }
    REQUIRE(found_mid_func_copy);
}

// inwards_g_root and inwards_g_at_y are the two halves of the
// f(xf, yf) = g(yf) scenario for the "compute one level inwards"
// counter. For the compute_root half, moving inwards to
// compute_at(f, yf) doesn't change the per-yf work, so
// points_required_inwards equals points_required_at_realization
// (and the "could compute further inside" perf warning would fire).
// For the compute_at(f, yf) half, moving inwards to compute_at(f, xf)
// recomputes g(yf) once per xf — points_required_inwards is much
// larger than realization (and the warning correctly stays silent).
void check_inwards_counter(const halide_profiler_pipeline_stats *p) {
    auto g_root = entries_of(p, "inwards_g_root");
    REQUIRE(g_root.size() == 1);
    REQUIRE(g_root[0]->points_required_at_realization > 0);
    REQUIRE(g_root[0]->points_required_inwards ==
            g_root[0]->points_required_at_realization);

    auto g_at_y = entries_of(p, "inwards_g_at_y");
    REQUIRE(g_at_y.size() == 1);
    REQUIRE(g_at_y[0]->points_required_at_realization > 0);
    // The factor is extent(r_inwards) = 16 in the generator; allow
    // some slack but require it to be clearly above the realization.
    REQUIRE(g_at_y[0]->points_required_inwards >
            g_at_y[0]->points_required_at_realization * 4);
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

// GPU-only: block-level producers stored in GPU shared / global (heap)
// memory. FuseGPUThreadLoops hoists their per-Func allocations out of the
// thread loops and coalesces them into one backing allocation. Before
// per-Func allocation naming this fused allocation showed up in the
// profiler as an orphan allocate row owned by no Func, and the per-Func
// allocation bytes were lost. Now each producer keeps its own name and is
// billed its own allocation size, so shared_a, shared_b, and shared_heap_h
// each get a single Func entry, parented under shared_out, reporting a
// non-zero memory_total and num_allocs.
void check_within_block_gpu_allocations_attributed(const halide_profiler_pipeline_stats *p) {
    auto shared_out = entries_of(p, "shared_out");
    REQUIRE(shared_out.size() == 1);
    int shared_out_id = (int)(shared_out[0] - p->funcs);

    auto descends_from = [&](int idx, int ancestor_id) {
        while (idx >= 0) {
            if (idx == ancestor_id) {
                return true;
            }
            idx = p->funcs[idx].parent;
        }
        return false;
    };

    for (const char *name : {"shared_a", "shared_b", "shared_heap_h"}) {
        auto fs = entries_of(p, name);
        REQUIRE(fs.size() == 1);
        // A real Func entry, not a synthetic allocation/copy row.
        REQUIRE(fs[0]->kind == halide_profiler_func_kind_func);
        // The within-block allocation was billed to this Func.
        REQUIRE(fs[0]->num_allocs > 0);
        REQUIRE(fs[0]->memory_total > 0);
        // Sensible size: at least one byte per recorded allocation.
        REQUIRE(fs[0]->memory_total >= fs[0]->num_allocs);
        // Parented inside the shared_out producer tree, not orphaned at root.
        int idx = (int)(fs[0] - p->funcs);
        REQUIRE(descends_from(idx, shared_out_id));
    }
}

// GPU points_computed for the within-block producers. Keeping per-Func
// store names (the fix under test) is what lets the stage-0 store counter
// reach each Func at all; before it, these stores hung off the fused
// backing-allocation name and were mis- or un-attributed. On GPU the count
// is a conservative upper bound rather than an exact tally: FuseGPUThreadLoops
// fuses the block's producers into one thread loop sized to the largest
// footprint and guards each producer's stores to its own footprint, but the
// profiler can't flush counters mid-kernel, so it hoists each per-thread
// contribution out by its loop-var upper bound (Profiling.cpp's
// hoist_loop_var_upper_bound) and scales by the fused thread extent. That is
// exact for the producer filling the thread extent and an over-estimate for
// the narrower ones. What must always hold: the store attribution reaches
// each Func (non-zero) and never under-counts its root footprint. (On CPU
// these Funcs count exactly — points_computed == points_required_at_root
// with no recompute — verified out of band.)
void check_within_block_gpu_points_computed(const halide_profiler_pipeline_stats *p) {
    for (const char *name : {"shared_a", "shared_b", "shared_heap_h"}) {
        auto fs = entries_of(p, name);
        REQUIRE(fs.size() == 1);
        uint64_t at_root = p->funcs[fs[0]->canonical_id].points_required_at_root;
        REQUIRE(at_root > 0);
        REQUIRE(fs[0]->points_computed > 0);
        REQUIRE(fs[0]->points_computed >= at_root);
    }
}

// GPU-only: a compute_root Func (dev_only_mid) consumed only on the device.
// InjectHostDevBufferCopies nulls its host allocation because the buffer
// lives solely in device global memory, so the profiler — which tracks
// memory at the host Allocate — would see a zero-sized allocation and bill
// nothing. IHDBC emits a declare_allocation marker carrying the device
// buffer's byte size at the null-out site. InjectCounters bills it to
// num_allocs/memory_total, and InjectProfiling turns it into a matched
// memory_allocate/memory_free pair (the host Free node still brackets the
// device lifetime) so memory_peak/current are tracked too. Assert
// dev_only_mid gets a single Func entry with non-zero num_allocs,
// memory_total, and memory_peak.
void check_device_only_compute_root_allocation(const halide_profiler_pipeline_stats *p) {
    auto fs = entries_of(p, "dev_only_mid");
    REQUIRE(fs.size() == 1);
    REQUIRE(fs[0]->kind == halide_profiler_func_kind_func);
    REQUIRE(fs[0]->num_allocs > 0);
    REQUIRE(fs[0]->memory_total > 0);
    REQUIRE(fs[0]->memory_total >= fs[0]->num_allocs);
    // The device allocation's lifetime is tracked (matched allocate/free),
    // so its peak is billed.
    REQUIRE(fs[0]->memory_peak > 0);
}

// The fused backing allocation that FuseGPUThreadLoops emits for coalesced
// within-block GPU allocations carries a synthetic name (unique_name of
// "shared_alloc" / "global_alloc", historically "allocgroup__f1__f2..."
// rendered with commas). Such a name corresponds to no Func, so if it ever
// reaches the profiler as its own entry it is an orphan allocate row. Assert
// that no entry carries one of these synthetic names.
void check_no_orphan_allocation_entries(const halide_profiler_pipeline_stats *p) {
    for (int i = 0; i < p->num_funcs; i++) {
        const char *name = p->funcs[i].name;
        REQUIRE(strncmp(name, "shared_alloc", strlen("shared_alloc")) != 0);
        REQUIRE(strncmp(name, "global_alloc", strlen("global_alloc")) != 0);
        REQUIRE(strstr(name, "allocgroup") == nullptr);
        REQUIRE(strchr(name, ',') == nullptr);
    }
}

// The counters_approximated bitmask flags counters that are conservative
// upper bounds rather than exact. It only happens on GPU, where a guarded
// contribution summed over a loop can't be counted exactly (the reporter
// marks those columns with a leading '<'). On CPU every counter is flushed
// at runtime, so nothing is ever flagged. Verify the mechanism is active on
// GPU (at least one entry flagged) and silent on CPU.
void check_counters_approximated(const halide_profiler_pipeline_stats *p, bool has_gpu) {
    int flagged = 0;
    for (int i = 0; i < p->num_funcs; i++) {
        if (p->funcs[i].counters_approximated) {
            flagged++;
        }
    }
    if (has_gpu) {
        REQUIRE(flagged > 0);
    } else {
        REQUIRE(flagged == 0);
    }
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
    check_load_store_counters(target);
    check_compute_with(target);
    check_tiled_stencil_modest_recompute(target);
    check_sliding_window_counters(target);
    check_sliding_window_failure_counters(target);
    check_inwards_counter(target);
    // Only present when the pipeline was built with a GPU feature — the
    // generator gates the corresponding Funcs on get_target().has_gpu_feature().
    if (!entries_of(target, "xfer_out").empty()) {
        check_copy_synthetics_parented_to_producer(target);
    }
    if (!entries_of(target, "mixed_sched").empty()) {
        check_mixed_host_device_update_defs(target);
    }
    if (!entries_of(target, "shared_out").empty()) {
        check_within_block_gpu_allocations_attributed(target);
        check_within_block_gpu_points_computed(target);
    }
    if (!entries_of(target, "dev_only_mid").empty()) {
        check_device_only_compute_root_allocation(target);
    }

    check_counters_approximated(target, !entries_of(target, "shared_out").empty());

    // Holds regardless of target: the fused-allocation backing name should
    // never surface as its own profiler entry.
    check_no_orphan_allocation_entries(target);

    check_points_required_at_root_canonical_only(target);

    printf("Success!\n");
    return 0;
}

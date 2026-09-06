#include "Halide.h"

using namespace Halide;

namespace {

// A pipeline exercising several profiler-entry scenarios in one pass:
//
//  - `multi_inlined` is inlined into two compute_root callers, so should get
//    two entry rows in the stats array sharing a canonical_id.
//  - `chain_a -> chain_b -> chain_c` form an inlining chain. Each link appears
//    in stats with the right parent pointing at the next link out.
//  - `update_f` has an update definition and no schedule; it gets a separate
//    Realize/Produce per caller, so two entry rows with the same name.
//  - `cse_shared(r)` is referenced in both the index and the value of an
//    update Provide, which Inline.cpp's Stmt-level CSE hoists into a LetStmt
//    above the Provide; this exercises the let-handling code in
//    resolve_inline_markers.
class ProfilerInstances : public Generator<ProfilerInstances> {
public:
    // Scalar input used to defeat monotonicity for the sliding-window
    // failure scenario. Always passed in as 1 by the aottest, but the
    // compiler can't see that, so sliding can't determine the sign.
    Input<int> stride{"stride"};
    Output<Buffer<int, 1>> out{"out"};

    void generate() {
        assert(get_target().has_feature(Target::Profile) ||
               get_target().has_feature(Target::ProfileByTimer));

        Var x;
        RDom r(0, 16);

        // Non-inlined Func with an unscheduled update def, reached from two
        // compute_root callers. The default-schedule lowering injects a
        // separate Realize/Produce inside each caller.
        Func update_f("update_f");
        update_f(x) = 0;
        update_f(x) += r;

        // RoundUp tail strategy on a compute_root Func: the loop trip
        // count is rounded up to a multiple of the factor and the body
        // writes the extra tail iterations unconditionally. Note that the
        // profiler's points_required_at_realization counter doesn't
        // surface this kind of over-computation (it reflects the Realize
        // box, which stays at the required extent) — only bytes_stored
        // does. We assert that gap below.
        Func roundup_outer("roundup_outer");
        roundup_outer(x) = x * 7;
        roundup_outer.compute_root();
        {
            Var xo, xi;
            roundup_outer.split(x, xo, xi, 10, TailStrategy::RoundUp);
        }

        // GuardWithIf tail strategy: same shape of split, but the tail
        // iterations are guarded so no extra writes happen. bytes_stored
        // matches points_required_at_realization × value-bytes.
        Func guard_outer("guard_outer");
        guard_outer(x) = x * 7;
        guard_outer.compute_root();
        {
            Var xo, xi;
            guard_outer.split(x, xo, xi, 10, TailStrategy::GuardWithIf);
        }

        // Compute-at-into-tile of a stencil producer: each tile of the
        // consumer reads a slightly wider box of the producer (halo), so
        // tile-boundary points get recomputed in adjacent tiles. The
        // producer's points_required_at_realization is modestly greater
        // than points_required_at_root.
        Func stencil_p("stencil_p"), stencil_out("stencil_out");
        stencil_p(x) = x * 13;
        stencil_out(x) = stencil_p(x - 1) + stencil_p(x) + stencil_p(x + 1);
        stencil_out.compute_root();
        {
            Var xo, xi;
            stencil_out.split(x, xo, xi, 16);
            stencil_p.compute_at(stencil_out, xo);
        }

        // Fully-unrolled Func with an update def. After unrolling there
        // are no stage-naming For loops at all in the IR — only the
        // declare_stage marker from ScheduleFunctions can tell pure-def
        // stores apart from update-def stores. We verify that
        // points_computed counts only the four pure-def stores, not the
        // four update-def stores.
        Func unrolled_pu("unrolled_pu");
        unrolled_pu(x) = 1;
        unrolled_pu(x) += 1;
        unrolled_pu.bound(x, 0, 4).unroll(x);
        unrolled_pu.update(0).unroll(x);
        unrolled_pu.compute_root();

        // compute_with: two Funcs share a loop nest, so their stage
        // stores appear interleaved in the IR. The per-Func pure-def
        // tracking has to attribute each Store to the right Func.
        Func cw_a("cw_a"), cw_b("cw_b");
        cw_a(x) = x;
        cw_b(x) = x * 2;
        cw_a.compute_root();
        cw_b.compute_root().compute_with(cw_a, x);

        // Sliding window: stencil producer compute_at an inner loop but
        // store_at an outer loop. Sliding rewrites each inner-iteration
        // produce box to just the leading edge after the first iteration,
        // so points_required_at_production summed per produce-firing equals
        // the realize-box per outer tile — both substantially less than
        // num_inner_iters * stencil_width (which is what you'd see if the
        // producer were compute_at the inner loop *without* store_at, and
        // what we'd want a future "sliding failed" warning to fire on).
        Func slide_p("slide_p"), slide_out("slide_out");
        slide_p(x) = x * 11;
        slide_out(x) = slide_p(x) + slide_p(x + 1) + slide_p(x + 2);
        slide_out.compute_root();
        {
            Var xo, xi;
            slide_out.split(x, xo, xi, 8);
            slide_p.store_at(slide_out, xo).compute_at(slide_out, xi);
        }

        // Sliding-window FAILURE: g is called at stride*x and stride*x+3.
        // Since `stride` is a runtime parameter the compiler can't see its
        // sign, the monotonicity check fails and sliding doesn't apply.
        // Each inner-loop iteration produces the full footprint of g, so
        // points_required_at_production sums much higher than the realize
        // box per outer tile. Note that the *footprint* (and so the
        // realize box) still depends on stride at compile time, but
        // bounds analysis still produces a finite bounding interval; we
        // just can't peel off all but the leading edge per iteration.
        Func slide_fail_g("slide_fail_g"), slide_fail_f("slide_fail_f");
        slide_fail_g(x) = x * 13;
        slide_fail_f(x) = slide_fail_g(stride * x) + slide_fail_g(stride * x + 3);
        slide_fail_f.compute_root();
        {
            Var xo, xi;
            slide_fail_f.split(x, xo, xi, 8);
            slide_fail_g.store_at(slide_fail_f, xo).compute_at(slide_fail_f, xi);
        }

        // GPU-only: trigger the "counters approximated" path. RDom::where
        // produces an IfThenElse inside the update body, and we put a
        // load from another Func into the predicate so the condition is
        // impure. Inside a GPU kernel this forces hoist_if onto the
        // max-of-branches path, which flags the Func's stats as
        // approximated. We assert the flag in the aottest.
        Func approx_pred("approx_pred"), approx_out("approx_out");
        if (get_target().has_gpu_feature()) {
            approx_pred(x) = x & 1;
            RDom r2(0, 8);
            r2.where(approx_pred(r2) != 0);
            approx_out(x) = 0;
            approx_out(x) += r2;
            Var xi;
            approx_pred.compute_root().gpu_tile(x, xi, 16);
            approx_out.compute_root().gpu_tile(x, xi, 16);
            approx_out.update(0).gpu_tile(x, xi, 16);
        }

        // GPU-only: outer CPU loop containing Funcs scheduled on both host
        // and device, forcing explicit halide_copy_to_host /
        // halide_copy_to_device calls. xfer_dev is a device producer
        // realized per outer iteration; xfer_host reads it from the host
        // (so a copy-to-host fires per outer iter); xfer_host is then
        // read by xfer_dev2 on the device (so a copy-to-device fires per
        // outer iter). The synthetic copy entries should be parented
        // to xfer_out and their realizations counter should match the
        // outer-loop trip count.
        Func xfer_dev("xfer_dev"), xfer_host("xfer_host"),
            xfer_dev2("xfer_dev2"), xfer_out("xfer_out");
        if (get_target().has_gpu_feature()) {
            xfer_dev(x) = x * 5;
            xfer_host(x) = xfer_dev(x) + 1;
            xfer_dev2(x) = xfer_host(x) * 2;
            xfer_out(x) = xfer_dev2(x);
            Var xo, xi;
            xfer_out.compute_root().split(x, xo, x, 32);
            xfer_dev.compute_at(xfer_out, xo).gpu_tile(x, xi, 8);
            xfer_host.compute_at(xfer_out, xo);
            xfer_dev2.compute_at(xfer_out, xo).gpu_tile(x, xi, 8);
        }

        // GPU-only: a Func with multiple update definitions, some
        // scheduled on the device and some left on the host. IHDBC has
        // to inject host<->device transfers between stages of the same
        // Func, which the report flags via a copy synthetic child whose
        // name matches its parent.
        Func mixed_sched("mixed_sched");
        if (get_target().has_gpu_feature()) {
            mixed_sched(x) = x;
            mixed_sched(x) += 1;
            mixed_sched(x) += 2;
            mixed_sched.compute_root();
            Var xi;
            mixed_sched.gpu_tile(x, xi, 8);
            // update(0) intentionally left on the host — this is the
            // user mistake we want to surface.
            mixed_sched.update(1).gpu_tile(x, xi, 8);
        }

        // Extern stage.
        Func extern_stage_e("extern_stage_e");
        extern_stage_e.define_extern("test_extern_stage",
                                     {Expr(2)}, Int(32), 1);
        extern_stage_e.compute_root();

        // Inwards-counter test:
        // f(xf, yf) = g(yf). g only depends on yf. With g.compute_root,
        // moving one level inwards to compute_at(f, yf) costs the same
        // (one g point per yf either way) — points_required_inwards
        // matches points_required_at_realization, so the "could compute
        // further inside" warning would fire. With g.compute_at(f, yf),
        // moving inwards to compute_at(f, xf) costs a factor of
        // extent(xf) (g recomputed per xf*yf instead of per yf) —
        // points_required_inwards is much larger than realization, so
        // the warning correctly stays silent.
        Var xf("xf"), yf("yf");
        Func inwards_g_root("inwards_g_root"), inwards_f_root("inwards_f_root");
        Func inwards_g_at_y("inwards_g_at_y"), inwards_f_at_y("inwards_f_at_y");
        inwards_g_root(yf) = yf * 3;
        inwards_f_root(xf, yf) = inwards_g_root(yf);
        inwards_g_root.compute_root();
        inwards_f_root.compute_root();

        inwards_g_at_y(yf) = yf * 3;
        inwards_f_at_y(xf, yf) = inwards_g_at_y(yf);
        inwards_g_at_y.compute_at(inwards_f_at_y, yf);
        inwards_f_at_y.compute_root();

        RDom r_inwards(0, 16);
        Func inwards_red_root("inwards_red_root"), inwards_red_at_y("inwards_red_at_y");
        inwards_red_root(x) = 0;
        inwards_red_root(x) += inwards_f_root(r_inwards, x);
        inwards_red_at_y(x) = 0;
        inwards_red_at_y(x) += inwards_f_at_y(r_inwards, x);

        Func caller_g("caller_g"), caller_h("caller_h");
        caller_g(x) = update_f(x);
        caller_h(x) = update_f(x) * 2;

        Expr out_value = caller_g(x) + caller_h(x) +
                         roundup_outer(x) + guard_outer(x) +
                         stencil_out(x) + unrolled_pu(x % 4) + cw_a(x) + cw_b(x) +
                         slide_out(x) + slide_fail_f(x) + extern_stage_e(x) +
                         inwards_red_root(x) + inwards_red_at_y(x);
        if (get_target().has_gpu_feature()) {
            out_value = out_value + approx_out(x) + xfer_out(x) + mixed_sched(x);
        }
        out(x) = out_value;

        caller_g.compute_root();
        caller_h.compute_root();
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ProfilerInstances, profiler_instances)

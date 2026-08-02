#pragma once

// Shared configure() scaffolding for every Approximation-based vec_dot
// Generator (the extended SymmetricVecDotGenerator, KQuantVecDotGenerator,
// LookupTableVecDotGenerator). All three build the same "naive fp32 dot
// product -> approximate_by both operands -> compute_offline severs the
// (already-quantized, Input-supplied) encode halves -> schedule" pipeline;
// they differ only in which block-indexed codecs their build_vec_dot() picks.
// This factors that shared body out via CRTP (Derived::build_vec_dot()) -- the
// same static-polymorphism idiom as codec_generator_base.h's
// CodecGeneratorBase.
//
// generate() never calls Approximation::encode()/decode() directly -- only
// through Func::approximate_by()/Pipeline::compute_offline(), exactly like the
// codec generators. The vec_dot is the point at which the framework's splice +
// sever path is exercised for a dot product (matching
// test/performance/matvec_offline_split.cpp).
//
// Usage:
//   class FooVecDotGenerator : public VecDotGeneratorBase<FooVecDotGenerator> {
//   public:
//       GeneratorParam<...> family{...};
//       VecDotSpec build_vec_dot() const { return {weight_codec, wbytes, act_codec, abytes, block_size, sched}; }
//   };

#include "Halide.h"

#include "codec_generator_base.h"  // Direction/SchemeAndBytes live here; shared idiom
#include "quant_components.h"
#include "sdot_schedule.h"

namespace ggml_halide {

// Whether the per-block scale factors out to a single block-invariant scalar
// (SDOT: hoist_invariants() + rfactor() + change_type(Int(32)) -> Int(32) inner
// dot) or not (Float: a plain vectorized float accumulation -- affine offsets,
// two-level sub-block scales, and per-group grid scales are not
// single-per-block-invariant).
enum class ScheduleKind { SDOT,
                          Float };

struct VecDotSpec {
    // Both codecs decode to a block-indexed (kk, blk) Func at the SAME
    // block_size, so the reduction below is uniform -- Wt(r.x, r.y) * Vec(r.x,
    // r.y). When an activation is stored in a smaller block than the weight
    // (e.g. Q1_0/NVFP4 x Q8_0), its codec is composed with a Reblock stage
    // (see quant_components.h) that re-views it at the weight's block_size --
    // the block-structure reconciliation is an Approximation, not something
    // the Generator open-codes into the reduction.
    std::unique_ptr<Halide::Approximation> weight_codec;
    int weight_bytes;
    std::unique_ptr<Halide::Approximation> act_codec;
    int act_bytes;
    int block_size;
    ScheduleKind sched;
    // When set (is_struct()), the corresponding operand's packed blocks are a
    // first-class 1-D Type::Struct buffer (one struct per block) rather than a
    // 2-D (byte, blk) UInt(8) buffer. Default-invalid keeps a codec on the byte
    // path -- so an operand whose codec isn't struct-typed (e.g. a Reblock'd
    // activation) just leaves its type unset.
    Halide::Type weight_type;
    Halide::Type act_type;
};

template<typename Derived>
class VecDotGeneratorBase : public Halide::Generator<Derived> {
public:
    // How many blocks the SDOT schedule keeps in flight as independent float
    // accumulators. Four is enough to hide the accumulate latency without
    // running the block loop out of vector registers.
    static constexpr int kUnrollBlocks = 4;

    void configure() {
        using namespace Halide;
        VecDotSpec spec = static_cast<Derived *>(this)->build_vec_dot();
        int bs = spec.block_size;

        // A struct-typed operand's packed blocks are a 1-D Type::Struct buffer
        // (block index only); a byte-path operand is 2-D (byte, blk).
        const bool wt_struct = spec.weight_type.is_struct();
        const bool act_struct = spec.act_type.is_struct();
        ImageParam x_blocks = wt_struct ? ImageParam(spec.weight_type, 1, "x_blocks") : ImageParam(UInt(8), 2, "x_blocks");
        ImageParam y_blocks = act_struct ? ImageParam(spec.act_type, 1, "y_blocks") : ImageParam(UInt(8), 2, "y_blocks");

        // The packed-block buffers are quantized GGML rows: their base pointers
        // are cache-line aligned. Without this Halide assumes 1-byte alignment
        // and lowers every strided / reinterpreted read (the interleaved fp16
        // scales, the int8 codes) to byte-wise ld1.b + orr reassembly instead
        // of wide vector loads -- which dominates these tiny vec_dots.
        x_blocks.set_host_alignment(64);
        y_blocks.set_host_alignment(64);

        // Naive fp32 placeholders -- never realized; compute_offline() severs
        // Acc from them entirely, and the real values come from the
        // already-quantized x_blocks/y_blocks. Block-indexed (kk, blk) to match
        // the codecs' block-indexed decode.
        Var kk("kk"), blk("blk"), u("u");
        Func Wt("wt_naive"), Vec("vec_naive");
        Wt(kk, blk) = 0.0f;
        Vec(kk, blk) = 0.0f;

        // The SDOT schedule interleaves kUnrollBlocks blocks into independent
        // accumulators, so it wants a block count divisible by that. Letting
        // Halide's split produce the odd tail instead is not a local cost: the
        // predicate it inserts makes the per-block sdot a dynamic-extent
        // allocation that has to be zeroed and accumulated through memory,
        // roughly doubling the cost of *every* block. So the main reduction gets
        // an exactly divisible extent and a second update sweeps the remainder
        // at the default schedule (at most kUnrollBlocks - 1 blocks).
        const bool sdot = spec.sched == ScheduleKind::SDOT;
        Expr nblocks = x_blocks.dim(wt_struct ? 0 : 1).extent();
        Expr main_blocks = sdot ? (nblocks / kUnrollBlocks) * kUnrollBlocks : nblocks;

        RDom r(0, bs, 0, main_blocks, "r");
        Func Acc("acc");
        Acc() = 0.0f;
        Acc() += Wt(r.x, r.y) * Vec(r.x, r.y);
        RDom r_tail(0, bs, main_blocks, nblocks - main_blocks, "r_tail");
        if (sdot) {
            Acc() += Wt(r_tail.x, r_tail.y) * Vec(r_tail.x, r_tail.y);
        }

        ApproximationResult wt_r = Wt.approximate_by(*spec.weight_codec, {Acc});
        ApproximationResult act_r = Vec.approximate_by(*spec.act_codec, {Acc});

        // Both operands' encode halves are severed and bound to the real
        // already-quantized Input buffers (same as symmetric_vec_dot). For the
        // extern-delegated / SeveredEncode weight schemes the encode is likewise
        // severed here, so its extern symbol is never computed or linked.
        std::vector<Func> to_sever = wt_r.encoded;
        to_sever.insert(to_sever.end(), act_r.encoded.begin(), act_r.encoded.end());
        std::vector<ImageParam> bind_to = {x_blocks, y_blocks};
        Pipeline({Acc}).compute_offline(to_sever, bind_to);

        // Only handles with update definitions (per-block stat reductions) need
        // explicit scheduling; pure pass-throughs stay inline (same reasoning as
        // symmetric_vec_dot_generator.cpp).
        for (Func h : wt_r.handles) {
            if (h.has_update_definition()) {
                h.compute_root();
            }
        }
        for (Func h : act_r.handles) {
            if (h.has_update_definition()) {
                h.compute_root();
            }
        }

        if (spec.sched == ScheduleKind::SDOT) {
            // The reduction is over (within-block r.x) x (block r.y). The lanes
            // of the accumulator come from r.x, so the sdot's four Int(32) lanes
            // survive all the way into the float accumulator and no block pays
            // for a horizontal reduce. They must come from r.x rather than r.y:
            // blocks are interleaved {scale, codes} records, so a lane per block
            // would gather both the codes and the scales, while a lane per
            // within-block group keeps every code load contiguous.
            //
            // One sdot consumes 16 int8s and lands in a 4-lane Int(32) register,
            // so r.x is cut three ways: chunks of 16 (one sdot each, run
            // serially so they accumulate into the *same* register), then within
            // a chunk a 4-wide lane index and the 4 elements the lane sums.
            // Reducing straight to 4 lanes instead would make Halide lower the
            // wide reduce as two independent sdots plus an addp to merge them --
            // an extra zeroing and an extra reduction per block.
            const int lanes = 4;
            const int chunk = 4 * lanes;
            RVar rxc("rxc"), rxr("rxr"), rxo("rxo"), rxi("rxi");
            Acc.update(0).split(r.x, rxc, rxr, chunk);
            Acc.update(0).split(rxr, rxo, rxi, 4);

            // sdot_partial() flattens both operands' decode chains and hoists
            // their per-block scales out of the surviving rxi reduction, leaving
            // the scale-free Int(32) dot. See sdot_schedule.h.
            Var lane("lane");
            Func Acc_i32 = sdot_partial(Acc, {{rxo, lane}, {r.y, u}}, {wt_r, act_r});

            // Acc's update now reduces over (rxo, r.y). Peel rxo back off as the
            // vector lanes, and peel kUnrollBlocks consecutive blocks off
            // alongside it into separate accumulators. The accumulators have to
            // be split over *blocks*: every lane of one accumulator advances on
            // every block, so widening the vector does not shorten the
            // multiply-add chain, only interleaving blocks does. At ~3-4 cycles
            // of accumulate latency, an un-interleaved chain is what bounds the
            // whole kernel.
            RVar ryo("ryo"), ryi("ryi");
            Var lv("lv"), bacc("bacc");
            Acc.update(0).split(r.y, ryo, ryi, kUnrollBlocks);
            Func acc_vec = Acc.update(0).rfactor({{rxo, lv}, {ryi, bacc}});
            acc_vec.compute_root().vectorize(lv, lanes).unroll(bacc);
            acc_vec.update().vectorize(lv, lanes).unroll(bacc);

            // Inside the unrolled body, not at the block-group loop: at `bacc` the
            // sdot is one block's worth of registers, whereas at `ryo` it is a
            // kUnrollBlocks-long buffer that Halide has to allocate, zero, and
            // accumulate through memory.
            Acc_i32.compute_at(acc_vec, bacc)
                .update()
                .atomic()
                .vectorize(rxi, 4)
                .vectorize(lane, lanes)
                .unroll(rxc);

            // Collapsing the lanes x unrolled-blocks accumulators is a fixed
            // cost, but at the row lengths GGML uses it is not a negligible one:
            // left alone it is a serial chain of lanes*unroll_blocks scalar
            // adds. Sum the blocks vectorially first, then reduce the lanes
            // horizontally, so it costs a handful of vector ops instead.
            Var lv2("lv2");
            Func acc_lanes = Acc.update(0).rfactor(rxo, lv2);
            acc_lanes.compute_root().vectorize(lv2, lanes);
            acc_lanes.update().vectorize(lv2, lanes);
            Acc.update(0).atomic().vectorize(rxo, lanes);

            // The odd-block tail deliberately keeps the default schedule.
            Acc.update(1).unscheduled();
        }
        // ScheduleKind::Float: leave the reduction at its default (legal) schedule
        // -- correctness first; an interleave/sub-block-aware performance schedule
        // is a separate step.

        Func result("result");
        result() = Acc();

        // A byte-path operand's block stride is pinned to its block width: these
        // are densely packed GGML rows, and leaving the stride dynamic costs a
        // serial pointer-add chain per block instead of an immediate offset.
        if (wt_struct) {
            x_blocks.dim(0).set_min(0);
        } else {
            x_blocks.dim(0).set_bounds(0, spec.weight_bytes);
            x_blocks.dim(1).set_min(0).set_stride(spec.weight_bytes);
        }
        if (act_struct) {
            y_blocks.dim(0).set_min(0);
        } else {
            y_blocks.dim(0).set_bounds(0, spec.act_bytes);
            y_blocks.dim(1).set_min(0).set_stride(spec.act_bytes);
        }

        this->add_input(x_blocks);
        this->add_input(y_blocks);
        this->add_output(result);
    }

    void generate() {
        // configure() built the whole pipeline (add_input/add_output included).
    }
};

}  // namespace ggml_halide

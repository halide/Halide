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
    // Whether the SDOT schedule must multiply the per-block product out before
    // hoisting. Set for the formats whose weight decode carries an offset (the
    // affine family): (d*code + m) * (d_act*act) has no single scale to hoist
    // until it is expanded. See sdot_schedule.h.
    bool distribute_terms;
    // When set (is_struct()), the corresponding operand's packed blocks are a
    // first-class 1-D Type::Struct buffer (one struct per block) rather than a
    // 2-D (byte, blk) UInt(8) buffer. Default-invalid keeps a codec on the byte
    // path -- so an operand whose codec isn't struct-typed (e.g. a Reblock'd
    // activation) just leaves its type unset.
    Halide::Type weight_type;
    Halide::Type act_type;
    // Set when the activation stores a per-block scaled sum of its codes (Q8_1's
    // `s` field). Together with distribute_terms (an affine weight), this lets
    // configure() sever the offset term's sum(act) accumulator to a stored fp16
    // field supplied as a third Input, instead of recomputing it -- ggml's own
    // q4_1/q5_1 optimization. See SchemeAndBytes::has_block_sums.
    bool act_has_block_sums = false;
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

        // Third Input, present only for the affine-x-Q8_1 pairings: the
        // activation's stored per-block scaled code sum (`s`). configure() severs
        // the offset term's sum(act) accumulator to this field rather than
        // recomputing it -- a zero-copy 1-D fp16 view of the `s` slot the ABI
        // wrapper passes (stride = block width). See the SDOT sever branch below.
        const bool sever_sum = spec.sched == ScheduleKind::SDOT &&
                               spec.distribute_terms && spec.act_has_block_sums;
        ImageParam s_blocks = sever_sum ? ImageParam(Float(16), 1, "s_blocks") : ImageParam();

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

        // The odd-block tail decodes through its OWN placeholder Funcs, given a
        // separate decode chain by a second approximate_by below. This lets the
        // main reduction materialize a reconstructed-codes leaf (Q5_x) via
        // compute_at while the tail -- a different loop nest that could not see
        // that per-block buffer -- reconstructs inline, at negligible cost (fewer
        // than kUnrollBlocks blocks). Both chains read the same x_blocks/y_blocks.
        Func WtT("wt_naive_tail"), VecT("vec_naive_tail");
        WtT(kk, blk) = 0.0f;
        VecT(kk, blk) = 0.0f;
        RDom r_tail(0, bs, main_blocks, nblocks - main_blocks, "r_tail");
        if (sdot) {
            Acc() += WtT(r_tail.x, r_tail.y) * VecT(r_tail.x, r_tail.y);
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
        if (sdot) {
            ApproximationResult wtT_r = WtT.approximate_by(*spec.weight_codec, {Acc});
            ApproximationResult actT_r = VecT.approximate_by(*spec.act_codec, {Acc});
            to_sever.insert(to_sever.end(), wtT_r.encoded.begin(), wtT_r.encoded.end());
            to_sever.insert(to_sever.end(), actT_r.encoded.begin(), actT_r.encoded.end());
            bind_to.push_back(x_blocks);
            bind_to.push_back(y_blocks);
            for (Func h : wtT_r.handles) {
                if (h.has_update_definition()) {
                    h.compute_root();
                }
            }
            for (Func h : actT_r.handles) {
                if (h.has_update_definition()) {
                    h.compute_root();
                }
            }
        }
        Pipeline({Acc}).compute_offline(to_sever, bind_to);

        // Q5_0/Q5_1 reconstruct each code from a nibble plus a per-element high
        // bit read from the qh field's byte->bits expansion table (see
        // PlanarBitPack::decode). That table read is only a *contiguous* 8-byte
        // load -- matching ggml's table_b2b -- when the qh byte is a scalar and
        // the 8 bit positions are the vector lanes. Inlined into the sdot it is
        // the opposite (qh byte per lane -> a per-lane gather), so the SDOT
        // branches materialize the reconstructed int8 codes per block (compute_at
        // the block loop, kk split as (byte, pos): pos vectorizes the table load,
        // byte unrolls to a scalar index). The odd-block tail decodes through its
        // own inline chain (see above), so it does not need this buffer.
        Func codes_leaf;
        for (const Func &h : wt_r.handles) {
            if (h.name() == "combine_bits_code") {
                codes_leaf = h;
                break;
            }
        }
        auto schedule_codes = [&](LoopLevel level) {
            // Split kk into (byte, pos): pos (8) vectorizes the contiguous table
            // load, byte unrolls to a scalar index. store_in(Register) keeps the
            // reconstructed codes in the vector register file straight into the
            // sdot instead of round-tripping a stack buffer (all stores are at
            // constant coordinates once ki is vectorized and ko unrolled).
            Var kc = codes_leaf.args()[0], co("co"), ci("ci"), byte("byte"), pos("pos");
            codes_leaf.compute_at(level)
                .store_in(MemoryType::Register)
                .split(kc, co, ci, 16)    // 16-code register unit = one sdot chunk
                .split(ci, byte, pos, 8)  // within it, two qh bytes x 8 positions
                .vectorize(pos, 8)
                .unroll(byte)
                .unroll(co);
        };

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

        if (sever_sum) {
            // Affine weight x Q8_1: the per-block product (d*code + m)*(d_act*act)
            // distributes into d*d_act*sum(code*act) + m*d_act*sum(act). ggml does
            // not recompute the second sum -- it reads the `s` field Q8_1 stores.
            // We do the same by severing that term's accumulator to the stored
            // field (the third Input, s_blocks), leaving only the Int(32) dot.
            //
            // rfactor to whole-block partials (variant A), inlining only the
            // WEIGHT's decode chain so the activation decode (Act) stays whole:
            // the offset term's accumulator is then sum_k Act(k, blk), which *is*
            // the stored `s`. The product term re-inlines Act's full chain and
            // re-hoists to recover the scale-free Int(32) dot.
            Func acc_dot = Acc.update().rfactor({{r.y, u}});

            std::vector<Func> winl = {wt_r.replacement};
            for (const Func &h : wt_r.handles) {
                // Keep the materialized codes leaf (Q5_1) out of the flatten, so
                // its qh table read stays a per-block contiguous load.
                if (h.function().can_be_inlined() &&
                    !(codes_leaf.defined() && h.name() == codes_leaf.name())) {
                    winl.push_back(h);
                }
            }
            for (size_t pass = 0; pass < winl.size(); pass++) {
                acc_dot.update().eager_inline(winl);
            }

            acc_dot.update().distribute();
            std::vector<Func> parts = acc_dot.update().hoist_invariants();
            // parts[0] = product term (scale * sum code_w*Act); parts[1] = offset
            // term (min * sum Act == stored s).

            // Sever the offset term to the stored fp16 field. change_type(Float16)
            // makes the severed accumulator's type match the data (the encoder
            // rounds `s` to fp16 too, so this reproduces ggml's own rounding);
            // compute_offline then replaces every call to it with a read of
            // s_blocks and discards the recomputing reduction.
            Func s16 = parts[1].change_type(Float(16));
            Pipeline({Acc}).compute_offline({s16}, {s_blocks});

            // Product term: flatten the activation's full decode chain and
            // re-hoist to pull d_act out, leaving the scale-free Int(32) dot.
            std::vector<Func> ainl = {act_r.replacement};
            for (const Func &h : act_r.handles) {
                if (h.function().can_be_inlined()) {
                    ainl.push_back(h);
                }
            }
            for (size_t pass = 0; pass < ainl.size(); pass++) {
                parts[0].update().eager_inline(ainl);
            }
            Func prod_i32 = parts[0].update().hoist_invariants()[0].change_type(Int(32));

            RVar ryo("ryo"), ryi("ryi");
            Var bacc("bacc");
            Acc.update(0).split(r.y, ryo, ryi, kUnrollBlocks);
            Func acc_vec = Acc.update(0).rfactor(ryi, bacc);
            acc_vec.compute_root().unroll(bacc);
            acc_vec.update().unroll(bacc);

            // Only the product dot is computed per block now; the offset term is
            // a severed read of s_blocks (nothing to schedule).
            prod_i32.compute_at(acc_vec, bacc).update().atomic().vectorize(r.x, bs);
            if (codes_leaf.defined()) {
                schedule_codes(LoopLevel(acc_vec, bacc));
            }
            Acc.update(1).unscheduled();
        } else if (spec.sched == ScheduleKind::SDOT && getenv("GGML_PER_BLOCK_PROBE")) {
            // PROBE (variant A): rfactor only the block index, so both terms are
            // whole-block reductions. Costs a horizontal reduce per block and a
            // scalar cross-block accumulator; kept for measuring the non-sum
            // formats against the lane-split default below.
            std::vector<Func> parts = sdot_partial(Acc, {{r.y, u}}, {wt_r, act_r}, spec.distribute_terms);

            RVar ryo("ryo"), ryi("ryi");
            Var bacc("bacc");
            Acc.update(0).split(r.y, ryo, ryi, kUnrollBlocks);
            Func acc_vec = Acc.update(0).rfactor(ryi, bacc);
            acc_vec.compute_root().unroll(bacc);
            acc_vec.update().unroll(bacc);

            for (Func &part : parts) {
                part.compute_at(acc_vec, bacc).update().atomic().vectorize(r.x, bs);
            }
            Acc.update(1).unscheduled();
        } else if (spec.sched == ScheduleKind::SDOT) {
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
            std::vector<std::string> keep_out;
            if (codes_leaf.defined()) {
                keep_out.push_back(codes_leaf.name());
            }
            std::vector<Func> Acc_i32 = sdot_partial(Acc, {{rxo, lane}, {r.y, u}}, {wt_r, act_r}, spec.distribute_terms, keep_out);

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
            for (Func &part : Acc_i32) {
                part.compute_at(acc_vec, bacc)
                    .update()
                    .atomic()
                    .vectorize(rxi, 4)
                    .vectorize(lane, lanes)
                    .unroll(rxc);
            }
            if (codes_leaf.defined()) {
                schedule_codes(LoopLevel(acc_vec, bacc));
            }

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
        if (sever_sum) {
            // A gathered view of the `s` slot within each packed block: the field
            // repeats every act_bytes, so the fp16 stride is act_bytes/2 (18 for
            // Q8_1's 36-byte block). Pinning it makes the per-block read a
            // compile-time immediate offset rather than a dynamic pointer chain.
            s_blocks.dim(0).set_min(0).set_stride(spec.act_bytes / 2);
        }

        this->add_input(x_blocks);
        this->add_input(y_blocks);
        if (sever_sum) {
            this->add_input(s_blocks);
        }
        this->add_output(result);
    }

    void generate() {
        // configure() built the whole pipeline (add_input/add_output included).
    }
};

}  // namespace ggml_halide

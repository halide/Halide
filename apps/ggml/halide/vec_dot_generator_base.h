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
};

template<typename Derived>
class VecDotGeneratorBase : public Halide::Generator<Derived> {
public:
    void configure() {
        using namespace Halide;
        VecDotSpec spec = static_cast<Derived *>(this)->build_vec_dot();
        int bs = spec.block_size;

        // dim 0: byte-within-block, dim 1: block index.
        ImageParam x_blocks(UInt(8), 2, "x_blocks");  // weight format
        ImageParam y_blocks(UInt(8), 2, "y_blocks");  // activation format

        // Naive fp32 placeholders -- never realized; compute_offline() severs
        // Acc from them entirely, and the real values come from the
        // already-quantized x_blocks/y_blocks. Block-indexed (kk, blk) to match
        // the codecs' block-indexed decode.
        Var kk("kk"), blk("blk"), u("u");
        Func Wt("wt_naive"), Vec("vec_naive");
        Wt(kk, blk) = 0.0f;
        Vec(kk, blk) = 0.0f;

        RDom r(0, bs, 0, x_blocks.dim(1).extent(), "r");
        Func Acc("acc");
        Acc() = 0.0f;
        Acc() += Wt(r.x, r.y) * Vec(r.x, r.y);

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
            // The per-block scale depends on the block index r.y, so it is only
            // invariant across the *within-block* reduction r.x. rfactor() runs
            // first, preserving r.y as u so the partial Acc_dot reduces over r.x
            // alone.
            Func Acc_dot = Acc.update().rfactor({{r.y, u}});

            // hoist_invariants() needs each operand's per-block scale to appear
            // as a top-level factor of Acc_dot's update product. A single
            // eager_inline() of the two .replacements only peels the outermost
            // relayout wrapper; the scale*codes product lives deeper, inside the
            // dequantizer Func the Approximation combinators build (see the SDOT
            // investigation). So fold in the *entire* decode chain of both
            // operands: eager_inline() no-ops on any Func not (currently)
            // directly called and flattens exposed calls left to right, so
            // inlining the whole set of inlinable decode handles, one pass per
            // possible chain level, flattens it regardless of build order --
            // leaving Acc_dot's update as (codes*scale)*(codes*scale) with the
            // scales as loop-invariant leaves.
            std::vector<Func> decode_funcs = {wt_r.replacement, act_r.replacement};
            for (const Func &h : wt_r.handles) {
                if (h.function().can_be_inlined()) {
                    decode_funcs.push_back(h);
                }
            }
            for (const Func &h : act_r.handles) {
                if (h.function().can_be_inlined()) {
                    decode_funcs.push_back(h);
                }
            }
            for (size_t pass = 0; pass < decode_funcs.size(); pass++) {
                Acc_dot.update().eager_inline(decode_funcs);
            }

            // hoist_invariants() lifts the now-invariant scales out of the r.x
            // sum; change_type() retypes the scale-free inner dot to Int(32),
            // leaving an SDOT-eligible integer dot (CodeGen_ARM matches it to
            // SDOT). See test/correctness/struct_type_dot_product.cpp for the
            // same q4_0 x q8_0 case written directly (single dequant Func).
            Func Acc_ff = Acc_dot.update().hoist_invariants();
            Func Acc_i32 = Acc_ff.change_type(Int(32));
            Acc_i32.compute_root()
                .update()
                .atomic()
                .vectorize(r.x, bs);
        }
        // ScheduleKind::Float: leave the reduction at its default (legal) schedule
        // -- correctness first; an interleave/sub-block-aware performance schedule
        // is a separate step.

        Func result("result");
        result() = Acc();

        x_blocks.dim(0).set_bounds(0, spec.weight_bytes);
        x_blocks.dim(1).set_min(0);
        y_blocks.dim(0).set_bounds(0, spec.act_bytes);
        y_blocks.dim(1).set_min(0);

        this->add_input(x_blocks);
        this->add_input(y_blocks);
        this->add_output(result);
    }

    void generate() {
        // configure() built the whole pipeline (add_input/add_output included).
    }
};

}  // namespace ggml_halide

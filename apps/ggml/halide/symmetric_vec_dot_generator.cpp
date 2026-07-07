// Generic, GeneratorParam-driven vec_dot for two GGML symmetric per-block
// affine formats (see quant_components.h). "Q4_0 x Q8_0" is not a distinct
// C++ class here -- it's one GENERATOR_ARGS instantiation of this generator,
// registered in CMakeLists.txt as q4_0_vec_dot.
//
// Unlike the old hand-written *VecDotGenerator classes (which duplicated the
// dequantize math already present in the matching *_generators.cpp file,
// and left the reduction entirely unscheduled), this builds a naive fp32 dot
// product, splices in both operands' quantize/dequantize round trips via
// Func::approximate_by(), severs the (already-quantized, Input-supplied)
// encode() halves away with Pipeline::compute_offline(), then uses
// Func::inline_calls() + rfactor's RFactorOptions::HoistInvariantFactor to
// reach a genuine Int(32) SDOT-eligible per-block accumulation -- the same
// recipe test/correctness/approximation_composition.cpp and
// test/performance/matvec_offline_split.cpp already validate, extended here
// to a *per-block* (not just per-row/global) scale via a preserved-RVar
// rfactor call, per test/correctness/rfactor.cpp's
// hoisted_rfactor_preserved_rvar_factor_test.
//
// generate() never calls Approximation::encode()/decode() directly -- only
// through Func::approximate_by().

#include "Halide.h"

#include "quant_components.h"

using namespace Halide;
using namespace ggml_halide;

namespace {

class SymmetricVecDotGenerator : public Generator<SymmetricVecDotGenerator> {
public:
    GeneratorParam<int> block_size{"block_size", 32};

    GeneratorParam<int> w_qmax{"w_qmax", 8};
    GeneratorParam<int> w_code_bits{"w_code_bits", 4};
    GeneratorParam<RoundingMode> w_rounding{
        "w_rounding",
        RoundingMode::TruncateHalfUpWithOffset,
        {{"nearest", RoundingMode::Nearest},
         {"truncate_half_up_with_offset", RoundingMode::TruncateHalfUpWithOffset}}};
    GeneratorParam<ScaleAnchor> w_anchor{
        "w_anchor",
        ScaleAnchor::ExtremeSignedValue,
        {{"abs_max", ScaleAnchor::AbsMax},
         {"extreme_signed", ScaleAnchor::ExtremeSignedValue}}};

    GeneratorParam<int> a_qmax{"a_qmax", 127};
    GeneratorParam<int> a_code_bits{"a_code_bits", 8};
    GeneratorParam<RoundingMode> a_rounding{
        "a_rounding",
        RoundingMode::Nearest,
        {{"nearest", RoundingMode::Nearest},
         {"truncate_half_up_with_offset", RoundingMode::TruncateHalfUpWithOffset}}};
    GeneratorParam<ScaleAnchor> a_anchor{
        "a_anchor",
        ScaleAnchor::AbsMax,
        {{"abs_max", ScaleAnchor::AbsMax},
         {"extreme_signed", ScaleAnchor::ExtremeSignedValue}}};

    // dim 0: byte-within-block, dim 1: block index.
    Input<Buffer<uint8_t, 2>> *x_blocks_;  // weight format
    Input<Buffer<uint8_t, 2>> *y_blocks_;  // activation format
    Output<Buffer<float, 0>> *result_;

    void configure() {
        x_blocks_ = add_input<Buffer<uint8_t, 2>>("x_blocks");
        y_blocks_ = add_input<Buffer<uint8_t, 2>>("y_blocks");
        result_ = add_output<Buffer<float, 0>>("result");
    }

    void generate() {
        int w_block_bytes = 2 + (w_code_bits == 4 ? (int)block_size / 2 : (int)block_size);
        int a_block_bytes = 2 + (a_code_bits == 4 ? (int)block_size / 2 : (int)block_size);

        auto wt_scheme = make_symmetric_block_codec(block_size, w_qmax, w_rounding, w_anchor, w_code_bits);
        auto act_scheme = make_symmetric_block_codec(block_size, a_qmax, a_rounding, a_anchor, a_code_bits);

        // "Obvious" naive placeholders -- never actually realized, since
        // compute_offline() severs Acc from depending on them at all. Real
        // values always come from the already-quantized x_blocks_/y_blocks_.
        Var kk("kk"), blk("blk");
        Func Wt("wt_naive"), Vec("vec_naive");
        Wt(kk, blk) = 0.0f;
        Vec(kk, blk) = 0.0f;

        Var u("u");
        RDom r(0, block_size, 0, x_blocks_->dim(1).extent(), "r");

        Func Acc("acc");
        Acc() = 0.0f;
        Acc() += Wt(r.x, r.y) * Vec(r.x, r.y);

        ApproximationResult wt_r = Wt.approximate_by(wt_scheme, {Acc});
        ApproximationResult act_r = Vec.approximate_by(act_scheme, {Acc});
        Acc.inline_calls({wt_r.replacement, act_r.replacement});

        std::vector<Func> to_sever = wt_r.encoded;
        to_sever.insert(to_sever.end(), act_r.encoded.begin(), act_r.encoded.end());
        std::vector<ImageParam> bind_to = {*x_blocks_, *y_blocks_};
        Pipeline({Acc}).compute_offline(to_sever, bind_to);

        // wt_r.handles/act_r.handles mix real scheduling handles (the
        // per-block stat reduction, which has an update definition and
        // can't be left inline) with pure pass-through Funcs Halide already
        // inlines by default -- only the former need explicit scheduling
        // (same reasoning as matvec_offline_split.cpp).
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

        // r.y (which block a term belongs to) is invariant for the
        // per-block scale factors inline_calls() just exposed, even though
        // it isn't invariant across the *whole* reduction -- preserving it
        // (rather than an empty preserved set) is what lets
        // HoistInvariantFactor find and hoist them despite that, per
        // test/correctness/rfactor.cpp's
        // hoisted_rfactor_preserved_rvar_factor_test. The intermediate then
        // reduces only over r.x (within a block), scaled once per block at
        // write-back -- a pure Int(32) dot product, eligible for SDOT.
        Func Acc_intm = Acc.update().rfactor({{r.y, u}}, RFactorOptions::HoistInvariantFactor);
        Acc_intm.compute_root()
            .update()
            .atomic()
            .vectorize(r.x, block_size);

        (*result_)() = Acc();

        x_blocks_->dim(0).set_bounds(0, w_block_bytes);
        x_blocks_->dim(1).set_min(0);
        y_blocks_->dim(0).set_bounds(0, a_block_bytes);
        y_blocks_->dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(SymmetricVecDotGenerator, symmetric_vec_dot)

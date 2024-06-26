#include <algorithm>

#include "AlignLoads.h"
#include "HexagonAlignment.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "ModulusRemainder.h"
#include "Scope.h"
#include "Simplify.h"
using std::vector;

namespace Halide {
namespace Internal {

namespace {

// This mutator attempts to rewrite unaligned or strided loads to
// sequences of aligned loads by loading aligned vectors that cover
// the original unaligned load, and then slicing or shuffling the
// intended vector out of the aligned vector.
class AlignLoads : public IRMutator {
public:
    AlignLoads(int alignment, int min_bytes)
        : alignment_analyzer(alignment), required_alignment(alignment), min_bytes_to_align(min_bytes) {
    }

private:
    HexagonAlignmentAnalyzer alignment_analyzer;

    // Loads and stores should ideally be aligned to the vector width in bytes.
    int required_alignment;

    // Minimum size of load to align.
    int min_bytes_to_align;

    using IRMutator::visit;

    // Rewrite a load to have a new index, updating the type if necessary.
    Expr make_load(const Load *load, const Expr &index, ModulusRemainder alignment) {
        internal_assert(is_const_one(load->predicate)) << "Load should not be predicated.\n";
        return mutate(Load::make(load->type.with_lanes(index.type().lanes()), load->name,
                                 index, load->image, load->param,
                                 const_true(index.type().lanes()),
                                 alignment));
    }

    Expr visit(const Load *op) override {
        if (!is_const_one(op->predicate)) {
            // TODO(psuriana): Do nothing to predicated loads for now.
            return IRMutator::visit(op);
        }

        if (!op->type.is_vector()) {
            // Nothing to do for scalar loads.
            return IRMutator::visit(op);
        }

        if (op->image.defined()) {
            // We can't reason about the alignment of external images.
            return IRMutator::visit(op);
        }

        if (required_alignment % op->type.bytes() != 0) {
            return IRMutator::visit(op);
        }

        if (op->type.bytes() * op->type.lanes() <= min_bytes_to_align) {
            // These can probably be treated as scalars instead.
            return IRMutator::visit(op);
        }

        Expr index = mutate(op->index);
        const Ramp *ramp = index.as<Ramp>();
        const int64_t *const_stride = ramp ? as_const_int(ramp->stride) : nullptr;
        if (!ramp || !const_stride) {
            // We can't handle indirect loads, or loads with
            // non-constant strides.
            return IRMutator::visit(op);
        }
        if (!(*const_stride == 1 || *const_stride == 2 || *const_stride == 3 || *const_stride == 4)) {
            // Handle ramps with stride 1, 2, 3 or 4 only.
            return IRMutator::visit(op);
        }

        int64_t aligned_offset = 0;
        bool is_aligned =
            alignment_analyzer.is_aligned(op, &aligned_offset);
        // We know the alignment_analyzer has been able to reason about alignment
        // if the following is true.
        bool known_alignment = is_aligned || (!is_aligned && aligned_offset != 0);
        int lanes = ramp->lanes;
        int native_lanes = required_alignment / op->type.bytes();
        int stride = static_cast<int>(*const_stride);
        if (stride != 1) {
            internal_assert(stride >= 0);

            // If we know the offset of this strided load is smaller
            // than the stride, we can just make the load aligned now
            // without requiring more vectors from the dense
            // load. This makes loads like f(2*x + 1) into an aligned
            // load of double length, with a single shuffle.
            int shift = known_alignment && aligned_offset < stride ? aligned_offset : 0;

            // Load a dense vector covering all of the addresses in the load.
            Expr dense_base = simplify(ramp->base - shift);
            ModulusRemainder alignment = op->alignment - shift;
            Expr dense_index = Ramp::make(dense_base, 1, lanes * stride);
            Expr dense = make_load(op, dense_index, alignment);

            // Shuffle the dense load.
            return Shuffle::make_slice(dense, shift, stride, lanes);
        }

        // We now have a dense vector load to deal with.
        internal_assert(stride == 1);
        if (lanes < native_lanes) {
            // This load is smaller than a native vector. Load a
            // native vector.
            Expr ramp_base = ramp->base;
            ModulusRemainder alignment = op->alignment;
            int slice_offset = 0;

            // If load is smaller than a native vector and can fully fit inside of it and offset is known,
            // we can simply offset the native load and slice.
            if (!is_aligned && aligned_offset != 0 && Int(32).can_represent(aligned_offset) && (aligned_offset + lanes <= native_lanes)) {
                ramp_base = simplify(ramp_base - (int)aligned_offset);
                alignment = alignment - aligned_offset;
                slice_offset = aligned_offset;
            }

            Expr native_load = make_load(op, Ramp::make(ramp_base, 1, native_lanes), alignment);

            // Slice the native load.
            return Shuffle::make_slice(native_load, slice_offset, 1, lanes);
        }

        if (lanes > native_lanes) {
            // This load is larger than a native vector. Load native
            // vectors, and concatenate the results.
            vector<Expr> slices;
            for (int i = 0; i < lanes; i += native_lanes) {
                int slice_lanes = std::min(native_lanes, lanes - i);
                Expr slice_base = simplify(ramp->base + i);
                ModulusRemainder alignment = op->alignment + i;
                slices.push_back(make_load(op, Ramp::make(slice_base, 1, slice_lanes), alignment));
            }
            return Shuffle::make_concat(slices);
        }

        if (!is_aligned && aligned_offset != 0 && Int(32).can_represent(aligned_offset)) {
            // We know the offset of this load from an aligned
            // address. Rewrite this is an aligned load of two
            // native vectors, followed by a shuffle.
            Expr aligned_base = simplify(ramp->base - (int)aligned_offset);
            ModulusRemainder alignment = op->alignment - (int)aligned_offset;
            Expr aligned_load = make_load(op, Ramp::make(aligned_base, 1, lanes * 2), alignment);

            return Shuffle::make_slice(aligned_load, (int)aligned_offset, 1, lanes);
        }

        return IRMutator::visit(op);
    }
};

}  // namespace

Stmt align_loads(const Stmt &s, int alignment, int min_bytes_to_align) {
    return AlignLoads(alignment, min_bytes_to_align).mutate(s);
}

}  // namespace Internal
}  // namespace Halide

#include <algorithm>

#include "AlignLoads.h"
#include "Bounds.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "ModulusRemainder.h"
#include "Scope.h"
#include "Simplify.h"
#include "HexagonAlignment.h"
using std::vector;

namespace Halide {
namespace Internal {

namespace {

// This mutator attempts to rewrite unaligned or strided loads to
// sequences of aligned loads by loading aligned vectors that cover
// the original unaligned load, and then slicing or shuffling the
// intended vector out of the aligned vector.
class AlignLoads : public IRMutator2 {
public:
    AlignLoads(int alignment, const Scope<ModulusRemainder>& alignment_info)
        : alignment_analyzer(alignment, alignment_info), required_alignment(alignment) {}

private:
    HexagonAlignmentAnalyzer alignment_analyzer;

    // Loads and stores should ideally be aligned to the vector width in bytes.
    int required_alignment;

    using IRMutator2::visit;

    // Rewrite a load to have a new index, updating the type if necessary.
    Expr make_load(const Load *load, Expr index) {
        internal_assert(is_one(load->predicate)) << "Load should not be predicated.\n";
        return mutate(Load::make(load->type.with_lanes(index.type().lanes()), load->name,
                                 index, load->image, load->param, const_true(index.type().lanes())));
    }

    Expr visit(const Load *op) override {
        if (!is_one(op->predicate)) {
            // TODO(psuriana): Do nothing to predicated loads for now.
            return IRMutator2::visit(op);
        }

        if (!op->type.is_vector()) {
            // Nothing to do for scalar loads.
            return IRMutator2::visit(op);
        }

        if (op->image.defined()) {
            // We can't reason about the alignment of external images.
            return IRMutator2::visit(op);
        }

        Expr index = mutate(op->index);
        const Ramp *ramp = index.as<Ramp>();
        const int64_t *const_stride = ramp ? as_const_int(ramp->stride) : nullptr;
        if (!ramp || !const_stride) {
            // We can't handle indirect loads, or loads with
            // non-constant strides.
            return IRMutator2::visit(op);
        }
        if (!(*const_stride == 1 || *const_stride == 2 || *const_stride == 3)) {
            // Handle ramps with stride 1, 2 or 3 only.
            return IRMutator2::visit(op);
        }

        int64_t aligned_offset = 0;
        bool is_aligned = alignment_analyzer.is_aligned(op, &aligned_offset);
        // We know the alignement_analyzer has been able to reason about alignment
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
            Expr dense_index = Ramp::make(dense_base, 1, lanes*stride);
            Expr dense = make_load(op, dense_index);

            // Shuffle the dense load.
            return Shuffle::make_slice(dense, shift, stride, lanes);
        }

        // We now have a dense vector load to deal with.
        internal_assert(stride == 1);
        if (lanes < native_lanes) {
            // This load is smaller than a native vector. Load a
            // native vector.
            Expr native_load = make_load(op, Ramp::make(ramp->base, 1, native_lanes));

            // Slice the native load.
            return Shuffle::make_slice(native_load, 0, 1, lanes);
        }

        if (lanes > native_lanes) {
            // This load is larger than a native vector. Load native
            // vectors, and concatenate the results.
            vector<Expr> slices;
            for (int i = 0; i < lanes; i += native_lanes) {
                int slice_lanes = std::min(native_lanes, lanes - i);
                Expr slice_base = simplify(ramp->base + i);
                slices.push_back(make_load(op, Ramp::make(slice_base, 1, slice_lanes)));
            }
            return Shuffle::make_concat(slices);
        }

        if (!is_aligned && aligned_offset != 0 && Int(32).can_represent(aligned_offset)) {
            // We know the offset of this load from an aligned
            // address. Rewrite this is an aligned load of two
            // native vectors, followed by a shuffle.
            Expr aligned_base = simplify(ramp->base - (int)aligned_offset);
            Expr aligned_load = make_load(op, Ramp::make(aligned_base, 1, lanes*2));

            return Shuffle::make_slice(aligned_load, (int)aligned_offset, 1, lanes);
        }

        return IRMutator2::visit(op);
    }

    template<typename NodeType, typename LetType>
    NodeType visit_let(const LetType *op) {
        if (op->value.type() == Int(32)) {
            alignment_analyzer.push(op->name, op->value);
        }

        Expr value = mutate(op->value);
        NodeType body = mutate(op->body);

        if (op->value.type() == Int(32)) {
            alignment_analyzer.pop(op->name);
        }

        if (!value.same_as(op->value) || !body.same_as(op->body)) {
            return LetType::make(op->name, value, body);
        } else {
            return op;
        }
    }

    Expr visit(const Let *op) override { return visit_let<Expr>(op); }
    Stmt visit(const LetStmt *op) override { return visit_let<Stmt>(op); }
};

}  // namespace

Stmt align_loads(Stmt s, int alignment, const Scope<ModulusRemainder> &alignment_info) {
    return AlignLoads(alignment, alignment_info).mutate(s);
}

} // namespace Internal
} // namespace Halide

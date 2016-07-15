#include <algorithm>

#include "AlignLoads.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Bounds.h"
#include "ModulusRemainder.h"
#include "Simplify.h"

using std::vector;

namespace Halide {
namespace Internal {

namespace {

Expr slice_vector(Expr vec, Expr start, Expr stride, int lanes) {
    return Call::make(vec.type().with_lanes(lanes), Call::slice_vector,
                      { vec, start, stride, lanes }, Call::PureIntrinsic);
}

// This mutator attempts to rewrite unaligned or strided loads to
// sequences of aligned loads by loading aligned vectors that cover
// the original unaligned load, and then slicing or shuffling the
// intended vector out of the aligned vector.
class AlignLoads : public IRMutator {
public:
    AlignLoads(int alignment) : required_alignment(alignment) {}

private:
    // The desired alignment of a vector load.
    int required_alignment;

    // Alignment info for variables in scope.
    Scope<ModulusRemainder> alignment_info;

    using IRMutator::visit;

    // Rewrite a load to have a new index, updating the type if necessary.
    Expr make_load(const Load *load, Expr index) {
        return mutate(Load::make(load->type.with_lanes(index.type().lanes()), load->name,
                                 index, load->image, load->param));
    }

    void visit(const Load *op) {
        if (!op->type.is_vector()) {
            // Nothing to do for scalar loads.
            IRMutator::visit(op);
            return;
        }

        if (op->image.defined()) {
            // We can't reason about the alignment of external images.
            IRMutator::visit(op);
            return;
        }

        Expr index = mutate(op->index);
        const Ramp *ramp = index.as<Ramp>();
        const int64_t *const_stride = ramp ? as_const_int(ramp->stride) : nullptr;
        if (!ramp || !const_stride) {
            // We can't handle indirect loads, or loads with
            // non-constant strides.
            IRMutator::visit(op);
            return;
        }
        int lanes = ramp->lanes;
        int native_lanes = required_alignment / op->type.bytes();

        if (!(*const_stride == 1 || *const_stride == 2 || *const_stride == 3)) {
            // If the ramp isn't stride 1, 2, or 3, don't handle it.

            // TODO: We should handle reverse vector loads (stride ==
            // -1), maybe others as well.
            IRMutator::visit(op);
            return;
        }

        // If this is a parameter, the base_alignment should be
        // host_alignment. Otherwise, this is an internal buffer,
        // which we assume has been aligned to the required alignment.
        int aligned_offset = 0;
        bool known_alignment = false;
        int base_alignment =
            op->param.defined() ? op->param.host_alignment() : required_alignment;
        if (base_alignment % required_alignment == 0) {
            // We know the base is aligned. Try to find out the offset
            // of the ramp base from an aligned offset.
            known_alignment = reduce_expr_modulo(ramp->base, native_lanes, &aligned_offset,
                                                 alignment_info);
        }

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
            expr = slice_vector(dense, shift, stride, lanes);
            return;
        }

        // We now have a dense vector load to deal with.
        internal_assert(stride == 1);
        if (lanes < native_lanes) {
            // This load is smaller than a native vector. Load a
            // native vector.
            Expr native_load = make_load(op, Ramp::make(ramp->base, 1, native_lanes));

            // Slice the native load.
            expr = slice_vector(native_load, 0, 1, lanes);
            return;
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
            expr = Call::make(op->type, Call::concat_vectors, slices, Call::PureIntrinsic);
            return;
        }

        if (known_alignment && aligned_offset != 0) {
            // We know the offset of this load from an aligned
            // address. Rewrite this is an aligned load of two
            // native vectors, followed by a shuffle.
            Expr aligned_base = simplify(ramp->base - aligned_offset);
            Expr aligned_load = make_load(op, Ramp::make(aligned_base, 1, lanes*2));

            expr = slice_vector(aligned_load, aligned_offset, 1, lanes);
            return;
        }

        IRMutator::visit(op);
    }

    template<typename NodeType, typename LetType>
    void visit_let(NodeType &result, const LetType *op) {
        if (op->value.type() == Int(32)) {
            alignment_info.push(op->name, modulus_remainder(op->value, alignment_info));
        }

        Expr value = mutate(op->value);
        NodeType body = mutate(op->body);

        if (op->value.type() == Int(32)) {
            alignment_info.pop(op->name);
        }

        if (!value.same_as(op->value) || !body.same_as(op->body)) {
            result = LetType::make(op->name, value, body);
        } else {
            result = op;
        }
    }

    void visit(const Let *op) { visit_let(expr, op); }
    void visit(const LetStmt *op) { visit_let(stmt, op); }
};

}  // namespace

Stmt align_loads(Stmt s, int alignment) {
    return AlignLoads(alignment).mutate(s);
}

}
}

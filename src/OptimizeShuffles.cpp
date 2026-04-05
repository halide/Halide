#include "OptimizeShuffles.h"
#include "Bounds.h"
#include "CSE.h"
#include "CodeGen_Internal.h"
#include "ConciseCasts.h"
#include "ExprUsesVar.h"
#include "FindIntrinsics.h"
#include "HexagonAlignment.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Lerp.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"
#include <utility>

namespace Halide {
namespace Internal {

namespace {

using SpanQueryType = std::function<std::vector<int>(const Type &)>;

class OptimizeShuffles : public IRMutator {
    int lut_alignment;
    int native_vector_bits;
    SpanQueryType get_max_span_sizes;
    bool align_loads_with_native_vector;
    Scope<Interval> bounds;
    std::vector<std::pair<std::string, Expr>> lets;

    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::if_then_else) && op->args[0].type().is_vector()) {
            const Broadcast *b = op->args[0].as<Broadcast>();
            if (!b || b->value.type().is_vector()) {
                return op;
            }
        }
        return IRMutator::visit(op);
    }

    template<typename LetOrLetStmt>
    auto visit_let(const LetOrLetStmt *op) -> decltype(op->body) {
        // We only care about vector lets.
        if (op->value.type().is_vector()) {
            bounds.push(op->name, bounds_of_expr_in_scope(op->value, bounds));
        }
        auto node = IRMutator::visit(op);
        if (op->value.type().is_vector()) {
            bounds.pop(op->name);
        }
        return node;
    }

    Expr visit(const Let *op) override {
        lets.emplace_back(op->name, op->value);
        Expr expr = visit_let(op);
        lets.pop_back();
        return expr;
    }
    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    std::set<std::string> allocations_to_pad;
    Stmt visit(const Allocate *op) override {
        Stmt s = IRMutator::visit(op);
        if (allocations_to_pad.count(op->name)) {
            op = s.as<Allocate>();
            internal_assert(op);
            int padding = native_vector_bits / op->type.bits();  // One native vector
            return Allocate::make(op->name, op->type, op->memory_type,
                                  op->extents, op->condition,
                                  op->body, op->new_expr, op->free_function,
                                  std::max(op->padding, padding));
        } else {
            return s;
        }
    }

    Expr visit(const Load *op) override {
        if (!is_const_one(op->predicate)) {
            // TODO(psuriana): We shouldn't mess with predicated load for now.
            return IRMutator::visit(op);
        }
        if (!op->type.is_vector() || op->index.as<Ramp>()) {
            // Don't handle scalar or simple vector loads.
            return IRMutator::visit(op);
        }

        Expr index = mutate(op->index);
        Interval unaligned_index_bounds = bounds_of_expr_in_scope(index, bounds);
        if (unaligned_index_bounds.is_bounded()) {
            // We want to try both the unaligned and aligned
            // bounds. The unaligned bounds might fit in 256 elements,
            // while the aligned bounds do not.
            int align = lut_alignment / op->type.bytes();
            Interval aligned_index_bounds = {
                (unaligned_index_bounds.min / align) * align,
                ((unaligned_index_bounds.max + align) / align) * align - 1};
            ModulusRemainder alignment(align, 0);

            const int native_vector_size = native_vector_bits / op->type.bits();

            for (const auto &max_span_size : get_max_span_sizes(op->type)) {

                for (const Interval &index_bounds : {aligned_index_bounds, unaligned_index_bounds}) {
                    Expr index_span = span_of_bounds(index_bounds);
                    index_span = common_subexpression_elimination(index_span);
                    index_span = simplify(index_span);

                    if (can_prove(index_span < max_span_size)) {
                        // This is a lookup within an up to max_span_size element array. We
                        // can use dynamic_shuffle for this.
                        int const_extent = as_const_int(index_span) ? *as_const_int(index_span) + 1 : max_span_size;
                        if (align_loads_with_native_vector) {
                            const_extent = align_up(const_extent, native_vector_size);
                        }
                        Expr base = simplify(index_bounds.min);

                        // Load all of the possible indices loaded from the
                        // LUT. Note that for clamped ramps, this loads up to 1
                        // vector past the max, so we will add padding to the
                        // allocation accordingly (if we're the one that made it).
                        allocations_to_pad.insert(op->name);
                        Expr lut = Load::make(op->type.with_lanes(const_extent), op->name,
                                              Ramp::make(base, 1, const_extent),
                                              op->image, op->param, const_true(const_extent), alignment);

                        // Target dependent codegen needs to cast the type of index to what it accepts
                        index = simplify(index - base);
                        return Call::make(op->type, "dynamic_shuffle", {lut, index, 0, const_extent - 1}, Call::PureIntrinsic);
                    }
                    // Only the first iteration of this loop is aligned.
                    alignment = ModulusRemainder();
                }
            }
        }
        if (!index.same_as(op->index)) {
            return Load::make(op->type, op->name, index, op->image, op->param, op->predicate, op->alignment);
        } else {
            return op;
        }
    }

public:
    OptimizeShuffles(int lut_alignment, int native_vector_bits, SpanQueryType get_max_span_sizes, bool align_loads_with_native_vector)
        : lut_alignment(lut_alignment),
          native_vector_bits(native_vector_bits),
          get_max_span_sizes(std::move(get_max_span_sizes)),
          align_loads_with_native_vector(align_loads_with_native_vector) {
    }
};
}  // namespace

Stmt optimize_shuffles(Stmt s, int lut_alignment, int native_vector_bits, SpanQueryType get_max_span_sizes, bool align_loads_with_native_vector) {
    s = OptimizeShuffles(lut_alignment, native_vector_bits, std::move(get_max_span_sizes), align_loads_with_native_vector)(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide

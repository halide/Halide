#include "OptimizeShuffles.h"
#include "Bounds.h"
#include "Buffer.h"
#include "CSE.h"
#include "CodeGen_Internal.h"
#include "ConciseCasts.h"
#include "ConstantBounds.h"
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
#include "Target.h"
#include <utility>

namespace Halide {
namespace Internal {

namespace {

class OptimizeShuffles : public IRMutator {
    int lut_alignment;
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
            int padding = 128 / op->type.bytes();  // One native vector
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

            for (const Interval &index_bounds : {aligned_index_bounds, unaligned_index_bounds}) {
                Expr index_span = span_of_bounds(index_bounds);
                index_span = common_subexpression_elimination(index_span);
                index_span = simplify(index_span);

                if (can_prove(index_span < 256)) {
                    // This is a lookup within an up to 256 element array. We
                    // can use dynamic_shuffle for this.
                    int const_extent = as_const_int(index_span) ? *as_const_int(index_span) + 1 : 256;
                    Expr base = simplify(index_bounds.min);

                    // Load all of the possible indices loaded from the
                    // LUT. Note that for clamped ramps, this loads up to 1
                    // vector past the max, so we will add padding to the
                    // allocation accordingly (if we're the one that made it).
                    allocations_to_pad.insert(op->name);
                    Expr lut = op->with(Ramp::make(base, 1, const_extent), const_true(const_extent), alignment);

                    // We know the size of the LUT is not more than 256, so we
                    // can safely cast the index to 8 bit, which
                    // dynamic_shuffle requires.
                    index = simplify(cast(UInt(8).with_lanes(op->type.lanes()), index - base));
                    return Call::make(op->type, "dynamic_shuffle", {lut, index, 0, const_extent - 1}, Call::PureIntrinsic);
                }
                // Only the first iteration of this loop is aligned.
                alignment = ModulusRemainder();
            }
        }
        return op->with(index, op->predicate, op->alignment);
    }

public:
    OptimizeShuffles(int lut_alignment)
        : lut_alignment(lut_alignment) {
    }
};

class OptimizeSmallTableLookups : public IRMutator {
    const Target &target;
    Scope<ConstantInterval> bounds;
    Scope<const Allocate *> allocations;

    using IRMutator::visit;

    int max_entries(const Type &t) const {
        return std::min(max_dynamic_shuffle_table_size(t.element_of(), target), 256);
    }

    template<typename LetOrLetStmt>
    auto visit_let(const LetOrLetStmt *op) -> decltype(op->body) {
        Expr value = mutate(op->value);
        const Type &t = value.type();
        ScopedBinding<ConstantInterval> bind(t.is_int_or_uint() && !t.is_bool(),
                                             bounds, op->name,
                                             constant_integer_bounds(value, bounds));
        auto body = mutate(op->body);
        if (value.same_as(op->value) && body.same_as(op->body)) {
            return op;
        }
        return LetOrLetStmt::make(op->name, std::move(value), std::move(body));
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Stmt visit(const Allocate *op) override {
        ScopedBinding<const Allocate *> bind(allocations, op->name, op);
        return IRMutator::visit(op);
    }

    bool can_prove_index_span_safe_to_load(const Load *op, const ConstantInterval &ib) const {
        if (!ib.is_bounded() || op->param.defined()) {
            return false;
        }
        if (op->image.defined()) {
            // Embedded buffer shapes are asserted on entry to the pipeline.
            const halide_buffer_t *buf = op->image.raw_buffer();
            const int64_t elem_bytes = buf->type.bytes();
            const int64_t base = (int64_t)(buf->begin() - buf->host);
            const int64_t end = (int64_t)(buf->end() - buf->host);
            return elem_bytes == op->type.bytes() &&
                   base % elem_bytes == 0 &&
                   end % elem_bytes == 0 &&
                   ib.min >= base / elem_bytes &&
                   ib.max < end / elem_bytes;
        }
        if (const auto *alloc = allocations.find(op->name)) {
            return (*alloc)->type == op->type.element_of() &&
                   !(*alloc)->new_expr.defined() &&
                   ib.min >= 0 &&
                   ib.max < (*alloc)->constant_allocation_size();
        }
        return false;
    }

    static Expr make_lookup(const Type &t, const Expr &table, const Expr &index) {
        int n = table.type().lanes();
        Expr idx = cast(UInt(8, index.type().lanes()), index);
        return Call::make(t, Call::dynamic_shuffle, {table, idx, 0, n - 1}, Call::PureIntrinsic);
    }

    Expr visit(const Call *op) override {
        if (!op->is_intrinsic(Call::mux) || !op->type.is_vector()) {
            return IRMutator::visit(op);
        }
        const int n = (int)op->args.size() - 1;
        const Expr &index = op->args[0];
        const Type &it = index.type();
        if (n < 2 ||
            it.lanes() != op->type.lanes() ||
            !it.is_int_or_uint() ||
            it.bits() < 8 ||
            n > max_entries(op->type)) {
            return IRMutator::visit(op);
        }

        std::vector<Expr> table;
        table.reserve(n);
        for (int i = 1; i <= n; i++) {
            Expr v = op->args[i];
            if (const Broadcast *b = v.as<Broadcast>()) {
                v = b->value;
            }
            if (!v.type().is_scalar() ||
                !(v.as<IntImm>() || v.as<UIntImm>() || v.as<FloatImm>())) {
                return IRMutator::visit(op);
            }
            table.push_back(v);
        }

        Expr idx = mutate(index);
        if (!ConstantInterval(0, n - 1).contains(constant_integer_bounds(idx, bounds))) {
            // mux returns the last value for any out-of-range index,
            // including negative ones, so clamp as unsigned.
            Type ut = it.with_code(halide_type_uint);
            idx = min(reinterpret(ut, idx), make_const(ut, n - 1));
        }
        return make_lookup(op->type, Shuffle::make_concat(table), idx);
    }

    Expr visit(const Load *op) override {
        if (!op->type.is_vector() ||
            op->index.as<Ramp>() ||
            op->index.as<Broadcast>() ||
            !is_const_one(op->predicate)) {
            return IRMutator::visit(op);
        }
        const int max_n = max_entries(op->type);
        if (max_n < 2) {
            return IRMutator::visit(op);
        }

        Expr index = mutate(op->index);
        ConstantInterval ib = constant_integer_bounds(index, bounds);
        if (ib.is_bounded() && ib.min < ib.max && ib.max - ib.min < max_n &&
            can_prove_index_span_safe_to_load(op, ib)) {
            int n = (int)(ib.max - ib.min + 1);
            Expr table_base = make_const(index.type().element_of(), ib.min);
            Expr table = op->with(Ramp::make(table_base, make_one(table_base.type()), n),
                                  const_true(n), ModulusRemainder());
            Expr idx = index - make_const(index.type(), ib.min);
            return make_lookup(op->type, table, idx);
        }
        if (index.same_as(op->index)) {
            return op;
        }
        return op->with(index, op->predicate, op->alignment);
    }

public:
    explicit OptimizeSmallTableLookups(const Target &target)
        : target(target) {
    }
};

}  // namespace

Stmt optimize_shuffles(Stmt s, int lut_alignment) {
    s = OptimizeShuffles(lut_alignment)(s);
    return s;
}

int max_dynamic_shuffle_table_size(const Type &t, const Target &target) {
    if (!t.is_int_or_uint() || t.bits() != 8) {
        return 0;
    }
    switch (target.arch) {
    case Target::ARM:
        // TODO: Use SVE2 tbl/tbl2 for scalable vectors.
        if (!target.features_any_of({Target::NoNEON, Target::SVE2, Target::SME2})) {
            return target.bits == 64 ? 64 : 32;
        }
        break;
    case Target::X86:
        if (target.has_feature(Target::AVX512_Cannonlake)) {
            return 64;
        } else if (target.has_feature(Target::SSE41)) {
            return 16;
        }
        break;
    case Target::WebAssembly:
        if (target.has_feature(Target::WasmSimd128)) {
            return 16;
        }
        break;
    default:
        break;
    }
    return 0;
}

Stmt optimize_small_table_lookups(const Stmt &s, const Target &target) {
    if (max_dynamic_shuffle_table_size(UInt(8), target) == 0) {
        return s;
    }
    return OptimizeSmallTableLookups(target)(s);
}

}  // namespace Internal
}  // namespace Halide

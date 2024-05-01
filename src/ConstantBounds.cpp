#include "ConstantBounds.h"
#include "IR.h"
#include "IROperator.h"
#include "IRPrinter.h"

namespace Halide {
namespace Internal {

namespace {
ConstantInterval bounds_helper(const Expr &e,
                               Scope<ConstantInterval> &scope,
                               std::map<Expr, ConstantInterval, ExprCompare> *cache) {
    internal_assert(e.defined());

    auto recurse = [&](const Expr &e) {
        return bounds_helper(e, scope, cache);
    };

    auto get_infinite_bounds = [&]() {
        // Compute the bounds of each IR node from the bounds of its args. Math
        // on ConstantInterval is in terms of infinite integers.
        if (const UIntImm *op = e.as<UIntImm>()) {
            if (Int(64).can_represent(op->value)) {
                return ConstantInterval::single_point((int64_t)(op->value));
            }
        } else if (const IntImm *op = e.as<IntImm>()) {
            return ConstantInterval::single_point(op->value);
        } else if (const Variable *op = e.as<Variable>()) {
            if (const auto *in = scope.find(op->name)) {
                return *in;
            }
        } else if (const Add *op = e.as<Add>()) {
            return recurse(op->a) + recurse(op->b);
        } else if (const Sub *op = e.as<Sub>()) {
            return recurse(op->a) - recurse(op->b);
        } else if (const Mul *op = e.as<Mul>()) {
            return recurse(op->a) * recurse(op->b);
        } else if (const Div *op = e.as<Div>()) {
            return recurse(op->a) / recurse(op->b);
        } else if (const Mod *op = e.as<Mod>()) {
            return recurse(op->a) % recurse(op->b);
        } else if (const Min *op = e.as<Min>()) {
            return min(recurse(op->a), recurse(op->b));
        } else if (const Max *op = e.as<Max>()) {
            return max(recurse(op->a), recurse(op->b));
        } else if (const Cast *op = e.as<Cast>()) {
            return recurse(op->value);
        } else if (const Broadcast *op = e.as<Broadcast>()) {
            return recurse(op->value);
        } else if (const VectorReduce *op = e.as<VectorReduce>()) {
            int f = op->value.type().lanes() / op->type.lanes();
            ConstantInterval factor(f, f);
            ConstantInterval arg_bounds = recurse(op->value);
            switch (op->op) {
            case VectorReduce::Add:
                return arg_bounds * factor;
            case VectorReduce::SaturatingAdd:
                return saturating_cast(op->type, arg_bounds * factor);
            case VectorReduce::Min:
            case VectorReduce::Max:
            case VectorReduce::And:
            case VectorReduce::Or:
                return arg_bounds;
            default:;
            }
        } else if (const Shuffle *op = e.as<Shuffle>()) {
            ConstantInterval arg_bounds = recurse(op->vectors[0]);
            for (size_t i = 1; i < op->vectors.size(); i++) {
                arg_bounds.include(recurse(op->vectors[i]));
            }
            return arg_bounds;
        } else if (const Let *op = e.as<Let>()) {
            ScopedBinding bind(scope, op->name, recurse(op->value));
            return recurse(op->body);
        } else if (const Call *op = e.as<Call>()) {
            ConstantInterval result;
            if (op->is_intrinsic(Call::abs)) {
                return abs(recurse(op->args[0]));
            } else if (op->is_intrinsic(Call::absd)) {
                return abs(recurse(op->args[0]) - recurse(op->args[1]));
            } else if (op->is_intrinsic(Call::count_leading_zeros) ||
                       op->is_intrinsic(Call::count_trailing_zeros)) {
                // Conservatively just say it's the potential number of zeros in the type.
                return ConstantInterval(0, op->args[0].type().bits());
            } else if (op->is_intrinsic(Call::halving_add)) {
                return (recurse(op->args[0]) + recurse(op->args[1])) / 2;
            } else if (op->is_intrinsic(Call::halving_sub)) {
                return (recurse(op->args[0]) - recurse(op->args[1])) / 2;
            } else if (op->is_intrinsic(Call::rounding_halving_add)) {
                return (recurse(op->args[0]) + recurse(op->args[1]) + 1) / 2;
            } else if (op->is_intrinsic(Call::saturating_add)) {
                return saturating_cast(op->type,
                                       (recurse(op->args[0]) +
                                        recurse(op->args[1])));
            } else if (op->is_intrinsic(Call::saturating_sub)) {
                return saturating_cast(op->type,
                                       (recurse(op->args[0]) -
                                        recurse(op->args[1])));
            } else if (op->is_intrinsic({Call::widening_add, Call::widen_right_add})) {
                return recurse(op->args[0]) + recurse(op->args[1]);
            } else if (op->is_intrinsic({Call::widening_sub, Call::widen_right_sub})) {
                return recurse(op->args[0]) - recurse(op->args[1]);
            } else if (op->is_intrinsic({Call::widening_mul, Call::widen_right_mul})) {
                return recurse(op->args[0]) * recurse(op->args[1]);
            } else if (op->is_intrinsic({Call::shift_right, Call::widening_shift_right})) {
                return recurse(op->args[0]) >> recurse(op->args[1]);
            } else if (op->is_intrinsic({Call::shift_left, Call::widening_shift_left})) {
                return recurse(op->args[0]) << recurse(op->args[1]);
            } else if (op->is_intrinsic({Call::rounding_shift_right, Call::rounding_shift_left})) {
                ConstantInterval ca = recurse(op->args[0]);
                ConstantInterval cb = recurse(op->args[1]);
                if (op->is_intrinsic(Call::rounding_shift_left)) {
                    cb = -cb;
                }
                ConstantInterval rounding_term = 1 << (cb - 1);
                // Note if cb is <= 0, rounding_term is zero.
                return (ca + rounding_term) >> cb;
            } else if (op->is_intrinsic(Call::mul_shift_right)) {
                ConstantInterval ca = recurse(op->args[0]);
                ConstantInterval cb = recurse(op->args[1]);
                ConstantInterval cq = recurse(op->args[2]);
                return (ca * cb) >> cq;
            } else if (op->is_intrinsic(Call::rounding_mul_shift_right)) {
                ConstantInterval ca = recurse(op->args[0]);
                ConstantInterval cb = recurse(op->args[1]);
                ConstantInterval cq = recurse(op->args[2]);
                ConstantInterval rounding_term = 1 << (cq - 1);
                return (ca * cb + rounding_term) >> cq;
            }
            // If you add a new intrinsic here, also add it to the expression
            // generator in test/correctness/lossless_cast.cpp
        }

        return ConstantInterval::bounds_of_type(e.type());
    };

    auto get_typed_bounds = [&]() {
        return cast(e.type(), get_infinite_bounds());
    };

    ConstantInterval ret;
    if (cache) {
        auto [it, cache_miss] = cache->try_emplace(e);
        if (cache_miss) {
            it->second = get_typed_bounds();
        }
        ret = it->second;
    } else {
        ret = get_typed_bounds();
    }

    internal_assert((!ret.min_defined || e.type().can_represent(ret.min)) &&
                    (!ret.max_defined || e.type().can_represent(ret.max)))
        << "constant_bounds returned defined bounds that are not representable in "
        << "the type of the Expr passed in.\n Expr: " << e << "\n Bounds: " << ret;

    return ret;
}
}  // namespace

ConstantInterval constant_integer_bounds(const Expr &e,
                                         const Scope<ConstantInterval> &scope,
                                         std::map<Expr, ConstantInterval, ExprCompare> *cache) {
    Scope<ConstantInterval> sub_scope;
    sub_scope.set_containing_scope(&scope);
    return bounds_helper(e, sub_scope, cache);
}

}  // namespace Internal
}  // namespace Halide

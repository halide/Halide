#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

// Miscellaneous expression visitors that are too small to bother putting in their own files


Expr Simplify::visit(const IntImm *op, ConstBounds *bounds) {
    if (bounds && no_overflow_int(op->type)) {
        bounds->min_defined = bounds->max_defined = true;
        bounds->min = bounds->max = op->value;
    }
    return op;
}

Expr Simplify::visit(const UIntImm *op, ConstBounds *bounds) {
    if (bounds && Int(64).can_represent(op->value)) {
        bounds->min_defined = bounds->max_defined = true;
        bounds->min = bounds->max = (int64_t)(op->value);
    }
    return op;
}

Expr Simplify::visit(const FloatImm *op, ConstBounds *bounds) {
    return op;
}

Expr Simplify::visit(const StringImm *op, ConstBounds *bounds) {
    return op;
}

Expr Simplify::visit(const Broadcast *op, ConstBounds *bounds) {
    Expr value = mutate(op->value, bounds);
    if (value.same_as(op->value)) {
        return op;
    } else {
        return Broadcast::make(value, op->type.lanes());
    }
}

Expr Simplify::visit(const Variable *op, ConstBounds *bounds) {
    if (bounds_info.contains(op->name)) {
        const ConstBounds &b = bounds_info.get(op->name);
        if (bounds) {
            *bounds = b;
        }
        if (b.min_defined && b.max_defined && b.min == b.max) {
            return make_const(op->type, b.min);
        }
    }

    if (var_info.contains(op->name)) {
        auto &info = var_info.ref(op->name);

        // if replacement is defined, we should substitute it in (unless
        // it's a var that has been hidden by a nested scope).
        if (info.replacement.defined()) {
            internal_assert(info.replacement.type() == op->type)
                << "Cannot replace variable " << op->name
                << " of type " << op->type
                << " with expression of type " << info.replacement.type() << "\n";
            info.new_uses++;
            // We want to remutate the replacement, because we may be
            // injecting it into a context where it is known to be a
            // constant (e.g. due to an if).
            return mutate(info.replacement, bounds);
        } else {
            // This expression was not something deemed
            // substitutable - no replacement is defined.
            info.old_uses++;
            return op;
        }
    } else {
        // We never encountered a let that defines this var. Must
        // be a uniform. Don't touch it.
        return op;
    }
}


Expr Simplify::visit(const Ramp *op, ConstBounds *bounds) {
    ConstBounds base_bounds, stride_bounds;
    Expr base = mutate(op->base, &base_bounds);
    Expr stride = mutate(op->stride, &stride_bounds);
    const int lanes = op->type.lanes();

    if (bounds && no_overflow_int(op->type)) {
        bounds->min_defined = base_bounds.min_defined && stride_bounds.min_defined;
        bounds->max_defined = base_bounds.max_defined && stride_bounds.max_defined;
        bounds->min = std::min(base_bounds.min, base_bounds.min + (lanes - 1) * stride_bounds.min);
        bounds->max = std::max(base_bounds.max, base_bounds.max + (lanes - 1) * stride_bounds.max);
    }

    // A somewhat torturous way to check if the stride is zero,
    // but it helps to have as many rules as possible written as
    // formal rewrites, so that they can be formally verified,
    // etc.
    auto rewrite = IRMatcher::rewriter(IRMatcher::ramp(base, stride, lanes), op->type);
    if (rewrite(ramp(x, 0), broadcast(x, lanes))) {
        return rewrite.result;
    }

    if (base.same_as(op->base) &&
        stride.same_as(op->stride)) {
        return op;
    } else {
        return Ramp::make(base, stride, op->lanes);
    }
}

Expr Simplify::visit(const Load *op, ConstBounds *bounds) {
    found_buffer_reference(op->name);

    Expr predicate = mutate(op->predicate, nullptr);
    Expr index = mutate(op->index, nullptr);

    const Broadcast *b_index = index.as<Broadcast>();
    const Broadcast *b_pred = predicate.as<Broadcast>();
    if (is_zero(predicate)) {
        // Predicate is always false
        return undef(op->type);
    } else if (b_index && b_pred) {
        // Load of a broadcast should be broadcast of the load
        Expr load = Load::make(op->type.element_of(), op->name, b_index->value, op->image, op->param, b_pred->value);
        return Broadcast::make(load, b_index->lanes);
    } else if (predicate.same_as(op->predicate) && index.same_as(op->index)) {
        return op;
    } else {
        return Load::make(op->type, op->name, index, op->image, op->param, predicate);
    }
}

}
}

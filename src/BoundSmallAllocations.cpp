#include "BoundSmallAllocations.h"
#include "Bounds.h"
#include "IRMutator.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

// Find a constant upper bound on the size of each thread-local allocation
class BoundSmallAllocations : public IRMutator2 {
    using IRMutator2::visit;

    // Track constant bounds
    Scope<Interval> scope;

    Stmt visit(const LetStmt *op) override {
        Interval b = find_constant_bounds(op->value, scope);
        ScopedBinding<Interval> bind(scope, op->name, b);
        return IRMutator2::visit(op);
    }

    Expr visit(const Let *op) override {
        Interval b = find_constant_bounds(op->value, scope);
        ScopedBinding<Interval> bind(scope, op->name, b);
        return IRMutator2::visit(op);
    }

    bool in_thread_loop = false;

    Stmt visit(const For *op) override {
        Interval min_bounds = find_constant_bounds(op->min, scope);
        Interval max_bounds = find_constant_bounds(op->min + op->extent - 1, scope);
        Interval b = Interval::make_union(min_bounds, max_bounds);
        b.min = simplify(b.min);
        b.max = simplify(b.max);
        ScopedBinding<Interval> bind(scope, op->name, b);
        ScopedValue<bool> old_in_thread_loop(in_thread_loop, in_thread_loop ||
                                             op->for_type == ForType::GPUThread);
        return IRMutator2::visit(op);
    }

    Stmt visit(const Allocate *op) override {
        Expr total_extent = make_const(Int(64), 1);
        for (const Expr &e : op->extents) {
            total_extent *= e;
        }
        Expr bound = find_constant_bound(total_extent, Direction::Upper, scope);
        user_assert(bound.defined() ||
                    (op->memory_type != MemoryType::Stack &&
                     op->memory_type != MemoryType::Register))
            << "Allocation " << op->name << " has a dynamic size. "
            << "Only fixed-size allocations can be stored on the stack or in registers. "
            << "Try storing on the heap instead.";
        user_assert(!in_thread_loop || bound.defined())
            << "Allocation " << op->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported on the gpu. "
            << "Try storing into shared memory instead.";
        // 128 bytes is a typical minimum allocation size in
        // halide_malloc. For now we are very conservative, and only
        // round sizes up to a constant if they're smaller than that.
        Expr malloc_overhead = 128 / op->type.bytes();
        if (bound.defined() &&
            (in_thread_loop ||
             op->memory_type == MemoryType::Stack ||
             op->memory_type == MemoryType::Register ||
             (op->memory_type == MemoryType::Auto && can_prove(bound <= malloc_overhead)))) {
            user_assert(can_prove(bound <= Int(32).max()))
                << "Allocation " << op->name << " has a size greater than 2^31: " << bound << "\n";
            bound = simplify(cast<int32_t>(bound));
            return Allocate::make(op->name, op->type, op->memory_type, {bound}, op->condition,
                                  mutate(op->body), op->new_expr, op->free_function);
        } else {
            return IRMutator2::visit(op);
        }
    }
};

Stmt bound_small_allocations(const Stmt &s) {
    return BoundSmallAllocations().mutate(s);
}

}  // namespace Internal
}  // namespace Halide

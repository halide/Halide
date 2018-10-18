#include "BoundSmallAllocations.h"
#include "Bounds.h"
#include "IRMutator.h"
#include "Simplify.h"
#include "CodeGen_Internal.h"

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
                    op->memory_type != MemoryType::Register)
            << "Allocation " << op->name << " has a dynamic size. "
            << "Only fixed-size allocations can be stored in registers. "
            << "Try storing on the heap or stack instead.";
        user_assert(!in_thread_loop || bound.defined())
            << "Allocation " << op->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported on the gpu. "
            << "Try storing into shared memory instead.";

        if (bound.defined()) {
            bound = simplify(cast<int32_t>(bound));
        }
        const int64_t *size_ptr = bound.defined() ? as_const_int(bound) : nullptr;
        int64_t size = size_ptr ? *size_ptr : 0;

        // 128 bytes is a typical minimum allocation size in
        // halide_malloc. For now we are very conservative, and only
        // round sizes up to a constant if they're smaller than that.
        int malloc_overhead = 128 / op->type.bytes();
        if (size_ptr &&
            (in_thread_loop ||
             (op->memory_type == MemoryType::Stack && can_allocation_fit_on_stack(size)) ||
             op->memory_type == MemoryType::Register ||
             (op->memory_type == MemoryType::Auto && size <= malloc_overhead))) {
            user_assert(size < (int64_t)1 << 31)
                << "Allocation " << op->name << " has a size greater than 2^31: " << bound << "\n";
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

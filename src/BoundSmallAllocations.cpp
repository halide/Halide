#include "BoundSmallAllocations.h"
#include "Bounds.h"
#include "IRMutator.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

// Find a constant upper bound on the size of each thread-local allocation
class BoundSmallAllocations : public IRMutator {
    using IRMutator::visit;

    // Track constant bounds
    Scope<Interval> scope;

    void visit(const LetStmt *op) {
        Interval b = find_constant_bounds(op->value, scope);
        scope.push(op->name, b);
        IRMutator::visit(op);
        scope.pop(op->name);
    }

    void visit(const Let *op) {
        Interval b = find_constant_bounds(op->value, scope);
        scope.push(op->name, b);
        IRMutator::visit(op);
        scope.pop(op->name);
    }

    bool in_thread_loop = false;

    void visit(const For *op) {
        Interval min_bounds = find_constant_bounds(op->min, scope);
        Interval max_bounds = find_constant_bounds(op->min + op->extent - 1, scope);
        Interval b = Interval::make_union(min_bounds, max_bounds);
        b.min = simplify(b.min);
        b.max = simplify(b.max);
        scope.push(op->name, b);
        if (op->for_type == ForType::GPUThread) {
            bool old_in_thread_loop = in_thread_loop;
            in_thread_loop = true;
            IRMutator::visit(op);
            in_thread_loop = old_in_thread_loop;
        } else {
            IRMutator::visit(op);
        }
        scope.pop(op->name);
    }

    void visit(const Allocate *op) {
        Expr total_extent = make_const(Int(64), 1);
        for (Expr e : op->extents) {
            total_extent *= e;
        }
        Expr bound = find_constant_bound(total_extent, Direction::Upper, scope);
        user_assert(!in_thread_loop || bound.defined())
            << "Allocation " << op->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported on the gpu. "
            << "Try storing into shared memory instead.";
        // 128 bytes is a typical minimum allocation size in
        // halide_malloc. For now we be very conservative, and only
        // round sizes up to a constant if they're smaller than that.
        Expr malloc_overhead = 128 / op->type.bytes();
        if (bound.defined() &&
            (in_thread_loop ||
             can_prove(bound <= malloc_overhead))) {
            user_assert(can_prove(bound <= Int(32).max()))
                << "Allocation " << op->name << " has a size greater than 2^31: " << bound << "\n";
            bound = simplify(cast<int32_t>(bound));
            stmt = Allocate::make(op->name, op->type, {bound}, op->condition,
                                  mutate(op->body), op->new_expr, op->free_function);
            return;
        } else {
            IRMutator::visit(op);
        }
    }
};

Stmt bound_small_allocations(Stmt s) {
    return BoundSmallAllocations().mutate(s);
}

}
}

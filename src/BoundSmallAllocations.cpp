#include "BoundSmallAllocations.h"
#include "Bounds.h"
#include "CodeGen_Internal.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

namespace {

// Find a constant upper bound on the size of each thread-local allocation
class BoundSmallAllocations : public IRMutator {
    using IRMutator::visit;

    // Track constant bounds
    Scope<Interval> scope;

    template<typename T, typename Body>
    Body visit_let(const T *op) {
        // Visit an entire chain of lets in a single method to conserve stack space.
        struct Frame {
            const T *op;
            ScopedBinding<Interval> binding;
            Frame(const T *op, Scope<Interval> &scope)
                : op(op),
                  binding(scope, op->name, find_constant_bounds(op->value, scope)) {
            }
        };
        std::vector<Frame> frames;
        Body result;

        do {
            result = op->body;
            frames.emplace_back(op, scope);
        } while ((op = result.template as<T>()));

        result = mutate(result);

        for (auto it = frames.rbegin(); it != frames.rend(); it++) {
            result = T::make(it->op->name, it->op->value, result);
        }

        return result;
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let<LetStmt, Stmt>(op);
    }

    Expr visit(const Let *op) override {
        return visit_let<Let, Expr>(op);
    }

    bool in_thread_loop = false;

    DeviceAPI device_api = DeviceAPI::None;

    Stmt visit(const For *op) override {
        Interval min_bounds = find_constant_bounds(op->min, scope);
        Interval max_bounds = find_constant_bounds(op->min + op->extent - 1, scope);
        Interval b = Interval::make_union(min_bounds, max_bounds);
        b.min = simplify(b.min);
        b.max = simplify(b.max);
        ScopedBinding<Interval> bind(scope, op->name, b);
        bool new_in_thread_loop =
            in_thread_loop || op->for_type == ForType::GPUThread;
        ScopedValue<bool> old_in_thread_loop(in_thread_loop, new_in_thread_loop);
        DeviceAPI new_device_api =
            op->device_api == DeviceAPI::None ? device_api : op->device_api;
        ScopedValue<DeviceAPI> old_device_api(device_api, new_device_api);
        return IRMutator::visit(op);
    }

    bool must_be_constant(MemoryType memory_type) const {
        return (memory_type == MemoryType::Register ||
                (device_api == DeviceAPI::OpenGLCompute &&
                 memory_type == MemoryType::GPUShared));
    }

    Stmt visit(const Realize *op) override {
        // Called pre-storage-flattening. At this point we just want
        // to ensure any extents on allocations which *must* be
        // constant *are* constant.
        if (must_be_constant(op->memory_type)) {
            Region region = op->bounds;
            bool changed = false;
            bool found_non_constant_extent = false;
            for (Range &r : region) {
                Expr bound = find_constant_bound(r.extent, Direction::Upper, scope);
                // We can allow non-constant extents for now, as long as all
                // remaining dimensions are 1 (so the stride is unused, which
                // will be non-constant).
                user_assert(!found_non_constant_extent || is_const_one(bound))
                    << "Was unable to infer constant upper bound on extent of realization "
                    << op->name << ". Use Func::bound_extent to specify it manually.";
                found_non_constant_extent = found_non_constant_extent || !bound.defined();
                if (bound.defined() && !bound.same_as(r.extent)) {
                    r.extent = bound;
                    changed = true;
                }
            }

            Stmt body = mutate(op->body);
            if (changed || !body.same_as(op->body)) {
                return Realize::make(op->name, op->types, op->memory_type, region, op->condition, body);
            } else {
                return op;
            }
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Allocate *op) override {
        Expr total_extent = make_const(Int(64), 1);
        for (const Expr &e : op->extents) {
            total_extent *= e;
        }
        Expr bound = find_constant_bound(total_extent, Direction::Upper, scope);

        if (!bound.defined() && must_be_constant(op->memory_type)) {
            user_assert(op->memory_type != MemoryType::Register)
                << "Allocation " << op->name << " has a dynamic size. "
                << "Only fixed-size allocations can be stored in registers. "
                << "Try storing on the heap or stack instead.";

            user_assert(!(device_api == DeviceAPI::OpenGLCompute &&
                          op->memory_type == MemoryType::GPUShared))
                << "Allocation " << op->name << " has a dynamic size. "
                << "Only fixed-size allocations can be stored in shared memory "
                << "in OpenGL compute shaders. Try storing in MemoryType::Heap "
                << "instead.";
        }

        const int64_t *size_ptr = bound.defined() ? as_const_int(bound) : nullptr;
        int64_t size = size_ptr ? *size_ptr : 0;

        if (size_ptr && size == 0 && !op->new_expr.defined()) {
            // This allocation is dead
            return Allocate::make(op->name, op->type, op->memory_type, {0}, const_false(),
                                  mutate(op->body), op->new_expr, op->free_function, op->padding);
        }

        // 128 bytes is a typical minimum allocation size in
        // halide_malloc. For now we are very conservative, and only
        // round sizes up to a constant if they're smaller than that.
        int malloc_overhead = 128 / op->type.bytes();
        if (size_ptr &&
            (in_thread_loop ||
             (op->memory_type == MemoryType::Stack && can_allocation_fit_on_stack(size)) ||
             must_be_constant(op->memory_type) ||
             (op->memory_type == MemoryType::Auto && size <= malloc_overhead))) {
            user_assert(size >= 0 && size < (int64_t)1 << 31)
                << "Allocation " << op->name << " has a size greater than 2^31: " << bound << "\n";
            return Allocate::make(op->name, op->type, op->memory_type, {(int32_t)size}, op->condition,
                                  mutate(op->body), op->new_expr, op->free_function, op->padding);
        } else {
            return IRMutator::visit(op);
        }
    }
};

}  // namespace

Stmt bound_small_allocations(const Stmt &s) {
    return BoundSmallAllocations().mutate(s);
}

}  // namespace Internal
}  // namespace Halide

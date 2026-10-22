#include "BoundSmallAllocations.h"
#include "Bounds.h"
#include "CodeGen_Internal.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Simplify.h"

#include <optional>

namespace Halide {
namespace Internal {

namespace {

// Find a constant upper bound on the size of each thread-local allocation
class BoundSmallAllocations : public IRMutator {
    using IRMutator::visit;

    // Track constant bounds
    Scope<Interval> scope;

    template<typename LetOrLetStmt>
    auto visit_let(const LetOrLetStmt *op) -> decltype(op->body) {
        // Visit an entire chain of lets in a single method to conserve stack space.
        struct Frame {
            const LetOrLetStmt *op;
            ScopedBinding<Interval> binding;
            Frame(const LetOrLetStmt *op, Scope<Interval> &scope)
                : op(op),
                  binding(scope, op->name, find_constant_bounds(op->value, scope)) {
            }
        };
        std::vector<Frame> frames;
        decltype(op->body) result;

        do {
            result = op->body;
            frames.emplace_back(op, scope);
        } while ((op = result.template as<LetOrLetStmt>()));

        result = mutate(result);

        for (const auto &frame : reverse_view(frames)) {
            result = frame.op->with(frame.op->value, result);
        }

        return result;
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    bool in_thread_loop = false;

    DeviceAPI device_api = DeviceAPI::None;

    Stmt visit(const For *op) override {
        Interval min_bounds = find_constant_bounds(op->min, scope);
        Interval max_bounds = find_constant_bounds(op->max, scope);
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
        return memory_type == MemoryType::Register ||
               memory_type == MemoryType::GPR;
    }

    // The realizations in MemoryType::GPR that we're inside of.
    Scope<> gpr_realizations;

    // MemoryType::GPR is the general-purpose registers of the host CPU, so
    // the Func can't be computed or used on any other device.
    void check_gpr_on_host(const std::string &name, const char *verb) const {
        if (device_api != DeviceAPI::None &&
            device_api != DeviceAPI::Host &&
            gpr_realizations.contains(name)) {
            user_error << "Func " << name << " is stored in MemoryType::GPR, but it is "
                       << verb << " in code that runs on device API " << device_api
                       << ". MemoryType::GPR is only supported for Funcs computed and "
                       << "used on the host CPU.\n";
        }
    }

    Stmt visit(const Provide *op) override {
        check_gpr_on_host(op->name, "computed");
        return IRMutator::visit(op);
    }

    Expr visit(const Call *op) override {
        if (op->call_type == Call::Halide) {
            check_gpr_on_host(op->name, "used");
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Realize *op) override {
        std::optional<ScopedBinding<>> bind_gpr;
        if (op->memory_type == MemoryType::GPR) {
            bind_gpr.emplace(gpr_realizations, op->name);
            check_gpr_on_host(op->name, "computed");
        }

        // Called pre-storage-flattening. At this point we just want
        // to ensure any extents on allocations which *must* be
        // constant *are* constant.
        if (must_be_constant(op->memory_type)) {
            Region region = op->bounds;
            bool changed = false;
            bool found_non_constant_extent = false;
            for (size_t i = 0; i < region.size(); i++) {
                Range &r = region[i];
                Expr bound = find_constant_bound(r.extent, Direction::Upper, scope);
                user_assert(bound.defined() || op->memory_type != MemoryType::GPR)
                    << "Func " << op->name << " is stored in MemoryType::GPR, but Halide was "
                    << "unable to infer a constant upper bound on the extent of dimension "
                    << i << " of its realization, which is " << r.extent << ". "
                    << "Allocations in MemoryType::GPR must have a constant size. "
                    << "Use Func::bound, Func::bound_extent, or Func::bound_storage "
                    << "to specify it.\n";
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
                return op->with(region, op->condition, body);
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
            user_assert(op->memory_type != MemoryType::Register &&
                        op->memory_type != MemoryType::GPR)
                << "Allocation " << op->name << " has a dynamic size. "
                << "Only fixed-size allocations can be stored in registers. "
                << "Try storing on the heap or stack instead.";
        }

        auto size_ptr = bound.defined() ? as_const_int(bound) : std::nullopt;
        int64_t size = size_ptr ? *size_ptr : 0;

        if (size_ptr && size == 0 && !op->new_expr.defined()) {
            // This allocation is dead
            return op->with({0}, const_false(), mutate(op->body));
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
            return op->with({(int32_t)size}, op->condition, mutate(op->body));
        } else {
            return IRMutator::visit(op);
        }
    }
};

}  // namespace

Stmt bound_small_allocations(const Stmt &s) {
    return BoundSmallAllocations()(s);
}

}  // namespace Internal
}  // namespace Halide

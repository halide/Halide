#include "ClampUnsafeAccesses.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IRPrinter.h"
#include "Simplify.h"

namespace Halide::Internal {

namespace {

struct ClampUnsafeAccesses : IRMutator {
    const std::map<std::string, Function> &env;
    FuncValueBounds &func_bounds;

    ClampUnsafeAccesses(const std::map<std::string, Function> &env, FuncValueBounds &func_bounds)
        : env(env), func_bounds(func_bounds) {
    }

protected:
    using IRMutator::visit;

    Expr visit(const Call *call) override {
        // TODO: should this be call->is_intrinsic()?
        if (call->call_type != Call::Halide) {
            return IRMutator::visit(call);
        }

        if (is_inside_indexing) {
            auto bounds = func_bounds.at({call->name, call->value_index});
            if (bounds_smaller_than_type(bounds, call->type)) {
                // TODO: check additional conditions for clamping h in f(x) = g(h(x))
                //   3. The schedule for f uses RoundUp or ShiftInwards
                //   4. h is not compute_at within f's produce node

                auto [new_args, changed] = mutate_with_changes(call->args);
                Expr new_call = changed ? call : Call::make(call->type, call->name, new_args, call->call_type, call->func, call->value_index, call->image, call->param);
                return Max::make(Min::make(new_call, std::move(bounds.max)), std::move(bounds.min));
            }
        }

        ScopedValue s(is_inside_indexing, true);
        return IRMutator::visit(call);
    }

    bool bounds_smaller_than_type(const Interval &bounds, Type type) {
        return bounds.is_bounded() && !(equal(bounds.min, type.min()) && equal(bounds.max, type.max()));
    }

private:
    bool is_inside_indexing = false;
};

}  // namespace

Stmt clamp_unsafe_accesses(const Stmt &s, const std::map<std::string, Function> &env, FuncValueBounds &func_bounds) {
    return ClampUnsafeAccesses(env, func_bounds).mutate(s);
}

}  // namespace Halide::Internal

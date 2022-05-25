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
    std::vector<std::string> realizes_inside_current_producer;

    ClampUnsafeAccesses(const std::map<std::string, Function> &env, FuncValueBounds &func_bounds)
        : env(env), func_bounds(func_bounds) {
    }

    bool is_realize_inside_current_producer(const std::string &n) const {
        return std::find(realizes_inside_current_producer.begin(),
                         realizes_inside_current_producer.end(), n) != realizes_inside_current_producer.end();
    }

protected:
    using IRMutator::visit;

    Stmt visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            ScopedValue<std::vector<std::string>> r(realizes_inside_current_producer, {});
            return IRMutator::visit(op);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Realize *op) override {
        realizes_inside_current_producer.push_back(op->name);
        auto new_stmt = IRMutator::visit(op);
        realizes_inside_current_producer.pop_back();
        return new_stmt;
    }

    Expr visit(const Let *let) override {
        return visit_let<Let, Expr>(let);
    }

    Stmt visit(const LetStmt *let) override {
        return visit_let<LetStmt, Stmt>(let);
    }

    Expr visit(const Variable *var) override {
        if (is_inside_indexing && let_var_inside_indexing.contains(var->name)) {
            let_var_inside_indexing.ref(var->name) = true;
        }
        return var;
    }

    Expr visit(const Call *call) override {
        // Note that if the call's realization is inside the current producer (i.e. it's caller),
        // the compute bounds of this call should be known to cover all loaded values, so we
        // should be able to safely skip the clamp injection (see #6297).
        if (call->call_type == Call::Halide && is_inside_indexing && !is_realize_inside_current_producer(call->name)) {
            auto bounds = func_bounds.at({call->name, call->value_index});
            if (bounds_smaller_than_type(bounds, call->type)) {
                // TODO(#6297): check that the clamped function's allocation bounds might be wider than its compute bounds
                auto [new_args, changed] = mutate_with_changes(call->args);
                Expr new_call = !changed ? call : Call::make(call->type, call->name, new_args, call->call_type, call->func, call->value_index, call->image, call->param);
                return Max::make(Min::make(new_call, std::move(bounds.max)), std::move(bounds.min));
            }
        }

        ScopedValue<bool> s(is_inside_indexing,
                            (is_inside_indexing ||
                             call->call_type == Call::Halide ||
                             call->call_type == Call::Image));
        return IRMutator::visit(call);
    }

private:
    template<typename L, typename Body>
    Body visit_let(const L *let) {
        ScopedBinding<bool> binding(let_var_inside_indexing, let->name, false);
        Body body = mutate(let->body);

        ScopedValue<bool> s(is_inside_indexing, is_inside_indexing || let_var_inside_indexing.get(let->name));
        Expr value = mutate(let->value);

        return L::make(let->name, std::move(value), std::move(body));
    }

    bool bounds_smaller_than_type(const Interval &bounds, Type type) {
        return bounds.is_bounded() && !(equal(bounds.min, type.min()) && equal(bounds.max, type.max()));
    }

    /**
     * A let-var is marked "true" if is used somewhere in an indexing expression.
     * visit_let will process its value binding with is_inside_indexing set when
     * this is the case.
     */
    Scope<bool> let_var_inside_indexing;
    bool is_inside_indexing = false;
};

}  // namespace

Stmt clamp_unsafe_accesses(const Stmt &s, const std::map<std::string, Function> &env, FuncValueBounds &func_bounds) {
    return ClampUnsafeAccesses(env, func_bounds).mutate(s);
}

}  // namespace Halide::Internal

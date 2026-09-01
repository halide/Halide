#ifndef HALIDE_RENAME_H
#define HALIDE_RENAME_H

/** \file
 * Defines a generic primitive for renaming the identifiers that IR nodes
 * carry directly, as opposed to substitute() (Substitute.h), which only
 * follows references to a name via Variable nodes. */

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "IR.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

/** Applies a renaming policy to every IR node that carries a name directly:
 * Variable, Let, LetStmt, For, Allocate, Free, Load, Store, and the buffer
 * name argument of image_load/image_store Call nodes. Never renames a
 * Variable that carries a Parameter or Buffer, since those are referenced
 * by identity, not by name.
 *
 * The policy is called at most once per distinct name; its result is
 * memoized and reused for every later occurrence of that name, so it is
 * safe (and typically the point) for the policy to have side effects, e.g.
 * calling unique_name().
 *
 * This is a flat, non-scope-aware rename: every occurrence of a given
 * original name anywhere in the subtree -- whether it's a binding
 * occurrence (the name on a Let/For/Allocate) or a use -- is replaced with
 * the same new name, regardless of which (if any) enclosing binder
 * introduced it. For this to be safe, the policy must be injective: it
 * must never map two different input names to the same output name. Given
 * that, renaming is safe under arbitrary nesting/shadowing, because it
 * relabels symbols without changing which binder each use resolves to.
 *
 * This does not, and cannot, eliminate shadowing: if two bindings that are
 * simultaneously in scope must end up with provably distinct names (e.g. to
 * satisfy some later pass that assumes distinct names never collide), use
 * uniquify_variable_names instead. */
template<typename Policy>
class Renamer : public IRMutator {
    Policy policy;
    std::unordered_map<std::string, std::string> renamed;

    using IRMutator::visit;

    Expr visit(const Variable *op) override {
        if (op->param.defined() || op->image.defined()) {
            return op;
        }
        const std::string &new_name = new_name_for(op->name);
        if (new_name == op->name) {
            return op;
        }
        return Variable::make(op->type, new_name, op->image, op->param, op->reduction_domain);
    }

    template<typename LetOrLetStmt>
    auto visit_let(const LetOrLetStmt *op) -> decltype(op->body) {
        const std::string &new_name = new_name_for(op->name);
        if (new_name == op->name) {
            return IRMutator::visit(op);
        }
        auto new_value = mutate(op->value);
        auto new_body = mutate(op->body);
        return LetOrLetStmt::make(new_name, new_value, new_body);
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Stmt visit(const For *op) override {
        const std::string &new_name = new_name_for(op->name);
        if (new_name == op->name) {
            return IRMutator::visit(op);
        }
        Expr new_min = mutate(op->min);
        Expr new_max = mutate(op->max);
        Stmt new_body = mutate(op->body);
        return For::make(new_name, new_min, new_max, op->for_type, op->partition_policy, op->device_api, new_body);
    }

    Stmt visit(const Allocate *op) override {
        const std::string &new_name = new_name_for(op->name);
        if (new_name == op->name) {
            return IRMutator::visit(op);
        }
        std::vector<Expr> new_extents = mutate(op->extents);
        Stmt new_body = mutate(op->body);
        Expr new_condition = mutate(op->condition);
        Expr new_new_expr = op->new_expr.defined() ? mutate(op->new_expr) : Expr();
        return Allocate::make(new_name, op->type, op->memory_type, new_extents,
                              new_condition, new_body, new_new_expr, op->free_function, op->padding);
    }

    Stmt visit(const Free *op) override {
        const std::string &new_name = new_name_for(op->name);
        if (new_name == op->name) {
            return IRMutator::visit(op);
        }
        return Free::make(new_name);
    }

    Expr visit(const Load *op) override {
        const std::string &new_name = new_name_for(op->name);
        if (new_name == op->name) {
            return IRMutator::visit(op);
        }
        return Load::make(op->type, new_name, mutate(op->index), op->image, op->param,
                          mutate(op->predicate), op->alignment, op->is_streaming);
    }

    Stmt visit(const Store *op) override {
        const std::string &new_name = new_name_for(op->name);
        if (new_name == op->name) {
            return IRMutator::visit(op);
        }
        return Store::make(new_name, mutate(op->value), mutate(op->index), op->param,
                           mutate(op->predicate), op->alignment, op->is_streaming);
    }

    Expr visit(const Call *op) override {
        // image_load/image_store carry the buffer name as args[0], a
        // StringImm possibly wrapped in a Broadcast for vectorized access.
        if (op->args.empty() ||
            !(op->is_intrinsic(Call::image_load) || op->is_intrinsic(Call::image_store))) {
            return IRMutator::visit(op);
        }
        const Broadcast *broadcast = op->args[0].as<Broadcast>();
        const StringImm *name_imm = broadcast ? broadcast->value.as<StringImm>() : op->args[0].as<StringImm>();
        if (!name_imm) {
            return IRMutator::visit(op);
        }
        const std::string &new_name = new_name_for(name_imm->value);
        if (new_name == name_imm->value) {
            return IRMutator::visit(op);
        }
        std::vector<Expr> new_args = op->args;
        Expr new_imm = StringImm::make(new_name);
        new_args[0] = broadcast ? Broadcast::make(new_imm, broadcast->lanes) : new_imm;
        for (size_t i = 1; i < new_args.size(); i++) {
            new_args[i] = mutate(new_args[i]);
        }
        return op->with(new_args);
    }

public:
    explicit Renamer(Policy policy)
        : policy(std::move(policy)) {
    }

    /** Get the new name for a given old name, computing and memoizing it via
     * the policy if this is the first time this name has been seen. */
    const std::string &new_name_for(const std::string &name) {
        auto it = renamed.find(name);
        if (it != renamed.end()) {
            return it->second;
        }
        return renamed.emplace(name, policy(name)).first->second;
    }
};

template<typename Policy>
Expr rename_ir(const Expr &e, Policy policy) {
    return Renamer<Policy>(std::move(policy))(e);
}

template<typename Policy>
Stmt rename_ir(const Stmt &s, Policy policy) {
    return Renamer<Policy>(std::move(policy))(s);
}

}  // namespace Internal
}  // namespace Halide

#endif

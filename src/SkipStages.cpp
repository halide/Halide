#include "SkipStages.h"
#include "Bounds.h"
#include "CSE.h"
#include "Debug.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"
#include "UniquifyVariableNames.h"

#include <iterator>
#include <utility>

namespace Halide {
namespace Internal {

// This lowering pass skips run produce nodes and sometimes allocating for
// stages where the result can't affect the output. It's essentially computation
// and allocation bounds inference but simpler. For each production, instead of
// inferring the bounds to compute, we want to infer a single boolean that tells
// us whether or not to run it. For each allocation, instead of inferring the
// region to allocate, we want to infer a single boolean that tells us whether
// or not to allocate.
//
// Like with bounds inference, if we infer this from scratch for each Func,
// using the conditions under which its consumers are computed, we'd get a
// quadratic blow-up in the size of these conditions as you go along the from
// the output to the inputs. Instead, for each stage, we want the condition
// under which it will be computed in terms of symbolic variables that signify
// whether or not its immediate consumers are going to be computed. These
// conditions can depend on loop variables, so we potentially need a fresh set
// of these variables at each loop level that contains produce nodes.

namespace {

// A prepass to rule out certain pieces of IR as uninteresting, to speed up the
// main pass.
class SkipStagesAnalysis : public IRVisitor {
    using IRVisitor::visit;

    // Is the visitor currently inside the condition of an IfThenElse or a
    // Select (note: *not* one of the branches - the condition itself)
    bool in_condition = false;

    // What is the nearest enclosing conditional node for the realize node of
    // each func. nullptr for outputs, because they don't have realize nodes.
    std::map<size_t, const IRNode *> conditional_around_realize_node;

    // What is the current nearest enclosing conditional node.
    const IRNode *enclosing_conditional = nullptr;

    void visit(const Select *op) override {
        {
            ScopedValue<bool> bind(in_condition, true);
            op->condition.accept(this);
        }
        {
            ScopedValue<const IRNode *> bind(enclosing_conditional, op);
            op->true_value.accept(this);
            op->false_value.accept(this);
        }
    }

    void visit(const IfThenElse *op) override {
        {
            ScopedValue<bool> bind(in_condition, true);
            op->condition.accept(this);
        }
        {
            ScopedValue<const IRNode *> bind(enclosing_conditional, op);
            op->then_case.accept(this);
            if (op->else_case.defined()) {
                op->else_case.accept(this);
            }
        }
    }

    // Have we encountered a Var or Call used inside a condition. If this
    // happens in the value field of a let, then that let name should also be
    // marked as interesting, because it could show up in a .used or .loaded
    // condition.
    bool found_var_used_in_condition = false;

    void visit(const LetStmt *op) override {
        op->body.accept(this);
        {
            ScopedValue<bool> bind(in_condition, in_condition ||
                                                     interesting_vars.count(op->name));
            found_var_used_in_condition = false;
            op->value.accept(this);
            if (found_var_used_in_condition) {
                // The value referred to a var or call that gets used in a
                // condition somewhere, therefore this LetStmt could also get
                // hoisted into a condition at some point.
                interesting_vars.insert(op->name);
            }
        }
    }

    void visit(const Let *op) override {
        op->body.accept(this);
        {
            ScopedValue<bool> bind(in_condition, in_condition ||
                                                     interesting_vars.count(op->name));
            bool old = found_var_used_in_condition;
            found_var_used_in_condition = false;
            op->value.accept(this);
            if (found_var_used_in_condition) {
                interesting_vars.insert(op->name);
            }
            // Is this expression interesting? I.e. might it show up in a .used
            // or .loaded? Either the body Expr was interesting in its own right
            // (refered to something used in a conditional somewhere), or the
            // value was interesting, and presumably the value is used in the
            // body.
            found_var_used_in_condition = found_var_used_in_condition || old;
        }
    }

    void visit(const Block *op) override {
        // Reverse order
        op->rest.accept(this);
        op->first.accept(this);
    }

    Scope<> in_produce;
    void visit(const ProducerConsumer *op) override {
        size_t id = func_id.at(op->name);

        if (op->is_producer &&
            !unconditionally_used_funcs.count(id) &&
            conditional_around_realize_node.count(id)) {
            // This node could have an if statement injected here
            ScopedValue<const IRNode *> s(enclosing_conditional, op);
            ScopedBinding<> bind(in_produce, op->name);
            op->body.accept(this);
        } else {
            // Either it's a consume, or it's used unconditionally outside of
            // and after this produce node (remember we're iterating in reverse
            // order), or it's an output (there is no enclosing realize node).
            op->body.accept(this);
        }
    }

    Scope<> in_realize;
    void visit(const Realize *op) override {
        size_t id = func_id.at(op->name);

        // There may have already been a Realize node for this Func. We need to
        // analyze this node from scratch.
        unconditionally_used_funcs.erase(id);

        conditional_around_realize_node[id] = enclosing_conditional;

        // Don't consider the realization bounds, which can't contain Func uses,
        // or the new or free exprs, which can't access Func data.
        {
            ScopedBinding<> bind(in_realize, op->name);
            op->body.accept(this);
        }

        if (conditionally_used_funcs.count(id)) {
            // Was used conditionally in a different Realize node, and used
            // unconditionally in this one.
            unconditionally_used_funcs.erase(id);
        } else if (!unconditionally_used_funcs.count(id)) {
            // Was used conditionally in this Realize node.
            conditionally_used_funcs.insert(id);
        }
    }

    void visit(const Call *op) override {
        if (op->call_type == Call::Halide) {
            if (in_condition) {
                interesting_vars.insert(op->name);
                found_var_used_in_condition = true;
            }
            size_t id = func_id.at(op->name);
            if (!in_produce.contains(op->name) &&
                enclosing_conditional == conditional_around_realize_node[id]) {
                unconditionally_used_funcs.insert(id);
            }
        }
        IRVisitor::visit(op);
    }

    void visit(const Variable *op) override {
        if (in_condition) {
            interesting_vars.insert(op->name);
            found_var_used_in_condition = true;
        }
        if (op->type.is_handle()) {
            auto it = func_id.find(op->name);
            if (it != func_id.end() &&
                in_realize.contains(op->name) &&
                !in_produce.contains(op->name) &&
                enclosing_conditional == conditional_around_realize_node[it->second]) {
                unconditionally_used_funcs.insert(it->second);
            }
        }
    }

public:
    SkipStagesAnalysis(const std::map<std::string, size_t> &func_id)
        : func_id(func_id) {
    }

    const std::map<std::string, size_t> func_id;

    // Vars which could conceivably end up in a skip-stages predicate. These are
    // the ones that are used (possibly transitively) in conditions in Select or
    // IfThenElse nodes.
    std::set<std::string> interesting_vars;

    // All Funcs that are unconditionally called within the scope of at least
    // one of their Realize nodes (and therefore could never be skipped so we
    // don't need to worry about them in the mutator below)
    std::set<size_t> unconditionally_used_funcs;

    // All Funcs that are conditionally called within the scope of at least one
    // of their Realize nodes, and therefore must not be added to
    // unconditionally_used_funcs.
    std::set<size_t> conditionally_used_funcs;
};

class SkipStages : public IRMutator {
public:
    SkipStages(const SkipStagesAnalysis &analysis, const std::vector<std::string> &name_for_id)
        : analysis(analysis), name_for_id(name_for_id) {
    }

protected:
    const SkipStagesAnalysis &analysis;
    const std::vector<std::string> &name_for_id;

    using IRMutator::visit;

    struct FuncInfo {
        // Condition under which values are used and need to be correct.
        Expr used;

        // Condition under which values are accessed, but don't need to be
        // correct. May be distinct from used if the calls to this Func are
        // guarded by selects.
        Expr loaded;
    };

    // Conditions for each Func that describe how it is used in the Stmt just
    // mutated, and any Stmts that come after it in the same enclosing loop
    // body. (TODO: worry about fork)
    std::map<size_t, FuncInfo> func_info;

    bool found_marker = false;

    // Might there be nested lets with the same name? Set to true if we ever
    // stamp down a .used let more than once for the same Func.
    bool need_uniquify = false;

    // Func ids for which we have ever stamped down a .used or .loaded let.
    std::set<size_t> lets_emitted;

    // Have we made use of .used or .loaded vars that haven't been wrapped in a
    // LetStmt yet (while iterating from inside out)?
    bool inner_unbound_use_of_used_or_loaded_vars = false;

    Stmt emit_defs(Stmt stmt) {
        for (auto &p : func_info) {
            stmt = LetStmt::make(used_var_name(p.first), simplify(p.second.used), stmt);
            stmt = LetStmt::make(loaded_var_name(p.first), simplify(p.second.loaded), stmt);
            need_uniquify |= !lets_emitted.insert(p.first).second;
        }
        return stmt;
    }

    Stmt visit(const Block *op) override {
        // We want to iterate in reverse, which really just requires changing
        // the block visitor.
        Stmt rest = mutate(op->rest);
        found_marker = false;
        Stmt first = mutate(op->first);
        if (found_marker) {
            // This is where the outermost .used definitions go
            internal_assert(first.as<Evaluate>());
            if (inner_unbound_use_of_used_or_loaded_vars) {
                rest = emit_defs(rest);
            }
            if (need_uniquify) {
                rest = uniquify_variable_names(rest);
            }
            return rest;
        }
        if (first.same_as(op->first) &&
            rest.same_as(op->rest)) {
            return op;
        } else {
            return Block::make(std::move(first), std::move(rest));
        }
    }

    Expr visit(const Call *op) override {
        if (op->name == "halide_memoization_cache_lookup") {
            // The buffer reference in a cache lookup doesn't count as a use -
            // it's an out parameter. However, do *do* need to conditionalize
            // the lookup on whether or not the buffer needs to be allocated.
            Expr last_arg = op->args.back();
            const Call *c = last_arg.as<Call>();
            internal_assert(c &&
                            c->is_intrinsic(Call::make_struct) &&
                            !c->args.empty())
                << last_arg;
            const Variable *v = c->args[0].as<Variable>();
            internal_assert(v);
            auto it = analysis.func_id.find(v->name);
            internal_assert(it != analysis.func_id.end());
            size_t func = it->second;
            if (func_info.find(func) != func_info.end()) {
                return Call::make(op->type, Call::if_then_else, {loaded_var(func), Expr(op), make_zero(op->type)}, Call::PureIntrinsic);
            } else {
                // Not in the func info map, so it must be unconditionally used.
                return op;
            }
        }

        Expr e = IRMutator::visit(op);
        if (op->call_type == Call::Halide) {
            size_t id = analysis.func_id.at(op->name);
            if (!analysis.unconditionally_used_funcs.count(id)) {
                // We're unconditionally used. Clobber any existing info.
                func_info[id] = FuncInfo{const_true(), const_true()};
            }
        } else if (op->is_intrinsic(Call::skip_stages_marker)) {
            found_marker = true;
        }
        return e;
    }

    Expr visit(const Variable *op) override {
        if (op->type == halide_type_of<halide_buffer_t *>()) {
            auto it = analysis.func_id.find(op->name);
            if (it != analysis.func_id.end() &&
                !analysis.unconditionally_used_funcs.count(it->second)) {
                // Conservatively assume any use of a .buffer symbol depends on
                // the Func being allocated and the values being correct.
                func_info[it->second] = FuncInfo{const_true(), const_true()};
            }
        }
        return op;
    }

    void merge_func_info(std::map<size_t, FuncInfo> *old,
                         const std::map<size_t, FuncInfo> &new_info,
                         const Expr &used = Expr{},
                         const Expr &evaluated = Expr{}) {
        for (const auto &it : new_info) {
            FuncInfo fi = it.second;
            if (used.defined()) {
                fi.used = fi.used && used;
            }
            if (evaluated.defined()) {
                fi.loaded = fi.loaded && evaluated;
            }
            auto [p, inserted] = old->try_emplace(it.first, fi);
            if (!inserted) {
                // Merge with any existing info
                if (!is_const_one(p->second.used)) {
                    p->second.used = p->second.used || fi.used;
                }
                if (!is_const_one(p->second.loaded)) {
                    p->second.loaded = p->second.loaded || fi.loaded;
                }
            }
        }
    }

    // Is an Expr safe to lift into a .used or .loaded condition.
    bool may_lift(const Expr &e) {
        class MayLift : public IRVisitor {
            using IRVisitor::visit;
            void visit(const Call *op) override {
                if (!op->is_pure() && op->call_type != Call::Halide) {
                    result = false;
                } else {
                    IRVisitor::visit(op);
                }
            }

        public:
            bool result = true;
        } v;
        e.accept(&v);
        return v.result;
    }

    // Come up with an upper bound for the truth value of an expression with the
    // given var eliminated.
    Expr relax_over_var(const Expr &e, const std::string &var) {
        Scope<Interval> domain;
        domain.push(var, Interval::everything());
        Interval in = bounds_of_expr_in_scope(e, domain);
        if (!in.has_upper_bound()) {
            return const_true();
        } else {
            return simplify(in.max);
        }
    }

    // Come up with an upper bound for the truth value of an expression with any
    // calls to the given func eliminated.
    Expr relax_over_calls(const Expr &e, const std::string &func) {
        class ReplaceCalls : public IRMutator {
            const std::string &func;

            using IRMutator::visit;

            Expr visit(const Call *op) override {
                if (op->call_type == Call::Halide && op->name == func) {
                    return cast(op->type, var);
                }
                return IRMutator::visit(op);
            }

        public:
            const std::string var_name;
            const Expr var;

            ReplaceCalls(const std::string &func)
                : func(func),
                  var_name(unique_name('t')),
                  var(Variable::make(Int(32), var_name)) {
            }
        } replacer(func);

        return relax_over_var(replacer.mutate(e), replacer.var_name);
    }

    Expr visit(const Select *op) override {
        if (!may_lift(op->condition)) {
            return IRMutator::visit(op);
        }

        std::map<size_t, FuncInfo> old;
        old.swap(func_info);
        mutate(op->true_value);
        merge_func_info(&old, func_info, op->condition);
        func_info.clear();
        mutate(op->false_value);
        merge_func_info(&old, func_info, !op->condition);
        old.swap(func_info);
        mutate(op->condition);  // Check for any calls in the condition

        return op;
    }

    Stmt mutate_conditional_stmt(const Stmt &s, const Expr &condition) {
        std::map<size_t, FuncInfo> old;
        old.swap(func_info);
        Stmt stmt = mutate(s);
        merge_func_info(&old, func_info, condition, condition);
        old.swap(func_info);
        return stmt;
    }

    Stmt visit(const IfThenElse *op) override {
        if (!may_lift(op->condition)) {
            // We won't be able to lift the condition
            return IRMutator::visit(op);
        }

        Stmt then_case = mutate_conditional_stmt(op->then_case, op->condition);
        Stmt else_case;
        if (op->else_case.defined()) {
            else_case = mutate_conditional_stmt(op->else_case, !op->condition);
        }
        mutate(op->condition);
        if (then_case.same_as(op->then_case) &&
            else_case.same_as(op->else_case)) {
            return op;
        } else {
            return IfThenElse::make(op->condition, then_case, else_case);
        }
    }

    template<typename T>
    auto visit_let(const T *op) -> decltype(op->body) {
        const T *orig = op;

        // Peel off any uninteresting lets without wasting stack frames.
        std::vector<std::pair<std::string, Expr>> containing_lets;
        decltype(op->body) body;
        while (op && !analysis.interesting_vars.count(op->name)) {
            containing_lets.emplace_back(op->name, op->value);
            body = op->body;
            op = body.template as<T>();
        }

        bool changed = false;
        if (op) {
            std::map<size_t, FuncInfo> old;
            old.swap(func_info);
            body = mutate(op->body);
            internal_assert(body.defined());
            if (may_lift(op->value)) {
                for (auto &it : func_info) {
                    if (expr_uses_var(it.second.used, op->name)) {
                        it.second.used = Let::make(op->name, op->value, it.second.used);
                    }
                    if (expr_uses_var(it.second.loaded, op->name)) {
                        it.second.loaded = Let::make(op->name, op->value, it.second.loaded);
                    }
                }
            } else {
                // Treat the let value as an unknown
                for (auto &it : func_info) {
                    if (expr_uses_var(it.second.used, op->name)) {
                        it.second.used = relax_over_var(it.second.used, op->name);
                    }
                    if (expr_uses_var(it.second.loaded, op->name)) {
                        it.second.loaded = relax_over_var(it.second.loaded, op->name);
                    }
                }
            }
            merge_func_info(&old, func_info);
            old.swap(func_info);
            mutate(op->value);
            if (body.same_as(op->body)) {
                body = op;
            } else {
                internal_assert(body.defined());
                body = T::make(op->name, op->value, std::move(body));
                changed = true;
            }
        } else if (std::is_same_v<T, LetStmt>) {
            auto new_body = mutate(body);
            changed = !new_body.same_as(body);
            body = std::move(new_body);
        } else {
            // Just visit the body
            mutate(body);
        }

        // Rewrap any uninteresting lets
        for (auto &[var, value] : reverse_view(containing_lets)) {
            mutate(value);  // Visit the value of each let
            if (changed) {
                body = T::make(var, std::move(value), std::move(body));
            }
        }

        if (changed) {
            internal_assert(body.defined());
            return body;
        } else {
            return orig;
        }
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    std::string used_var_name(size_t id) {
        return name_for_id[id] + ".used";
    }

    Expr used_var(size_t id) {
        return Variable::make(Bool(), used_var_name(id));
    }

    std::string loaded_var_name(size_t id) {
        return name_for_id[id] + ".loaded";
    }

    Expr loaded_var(size_t id) {
        return Variable::make(Bool(), loaded_var_name(id));
    }

    Scope<> in_realize;
    Scope<> in_realize_and_produce_or_consume;

    Stmt visit(const ProducerConsumer *op) override {
        size_t id = analysis.func_id.at(op->name);
        const bool unconditionally_used = analysis.unconditionally_used_funcs.count(id);

        if (op->is_producer && !unconditionally_used) {
            // The body of this is conditional, based on a yet-to-be defined symbolic value.
            Expr used = used_var(id);
            Stmt body;

            auto it = func_info.try_emplace(id, FuncInfo{const_false(), const_false()}).first;

            // Save the info about how this Func is called. We don't
            // care about self-calls in the produce node.
            FuncInfo fi = it->second;
            ScopedBinding<> bind_if(in_realize.contains(op->name),
                                    in_realize_and_produce_or_consume, op->name);

            body = mutate_conditional_stmt(op->body, used);
            // Restore the info about how this Func is called. Calls to
            // it in its own producer don't count towards skip stages
            // analysis.
            it->second = fi;
            body = IfThenElse::make(used, body);
            inner_unbound_use_of_used_or_loaded_vars = true;

            if (body.same_as(op->body)) {
                return op;
            } else {
                return ProducerConsumer::make(op->name, op->is_producer, std::move(body));
            }
        } else {
            ScopedBinding<> bind_if(!unconditionally_used &&
                                        in_realize.contains(op->name),
                                    in_realize_and_produce_or_consume, op->name);

            Stmt s = IRMutator::visit(op);

            if (analysis.interesting_vars.count(op->name)) {
                for (auto &p : func_info) {
                    p.second.used = relax_over_calls(p.second.used, op->name);
                    p.second.loaded = relax_over_calls(p.second.loaded, op->name);
                }
            }

            return s;
        }
    }

    Stmt visit(const Realize *op) override {
        size_t id = analysis.func_id.at(op->name);
        if (analysis.unconditionally_used_funcs.count(id)) {
            return IRMutator::visit(op);
        }

        Stmt body;
        {
            ScopedBinding<> bind(in_realize, op->name);
            body = mutate(op->body);
        }
        Expr condition = mutate(op->condition);
        auto it = func_info.find(id);
        if (it != func_info.end()) {
            if (!is_const_one(it->second.loaded)) {
                inner_unbound_use_of_used_or_loaded_vars = true;
                condition = condition && loaded_var(id);
            }
        }

        // We don't need to visit the bounds, because there can't be call nodes
        // in them.
        if (body.same_as(op->body) &&
            condition.same_as(op->condition)) {
            return op;
        } else {
            return Realize::make(op->name, op->types, op->memory_type,
                                 op->bounds, std::move(condition), std::move(body));
        }
    }

    bool in_vector_loop = false;

    Stmt visit(const For *op) override {
        ScopedValue<bool> s(in_vector_loop,
                            in_vector_loop || op->for_type == ForType::Vectorized);
        bool old_inner_unbound_uses = inner_unbound_use_of_used_or_loaded_vars;
        inner_unbound_use_of_used_or_loaded_vars = false;

        std::map<size_t, FuncInfo> old;
        old.swap(func_info);

        Stmt body;
        body = mutate(op->body);
        // There can't be calls in the min and extent, so no need to visit;
        // those.

        const bool in_sliding_loop = in_realize_and_produce_or_consume.size() < in_realize.size();
        bool may_emit =
            !in_vector_loop &&
            !in_sliding_loop &&
            inner_unbound_use_of_used_or_loaded_vars;

        Stmt body_before = body;
        if (may_emit) {
            body = emit_defs(body);
        }

        // Now relax all the conditions that depend on this loop variable.
        bool anything_depended_on_loop_var = false;
        for (auto &p : func_info) {
            if (expr_uses_var(p.second.used, op->name)) {
                p.second.used = relax_over_var(p.second.used, op->name);
                anything_depended_on_loop_var = true;
            }
            if (expr_uses_var(p.second.loaded, op->name)) {
                p.second.loaded = relax_over_var(p.second.loaded, op->name);
                anything_depended_on_loop_var = true;
            }
        }

        if (!anything_depended_on_loop_var) {
            // Adding definitions for .used and .loaded symbols is unnecessary
            // here. We can just use the ones one loop level further out.
            body = body_before;
        } else if (may_emit) {
            inner_unbound_use_of_used_or_loaded_vars = false;
        }
        inner_unbound_use_of_used_or_loaded_vars |= old_inner_unbound_uses;

        // To consider: Could add that the loop has non-zero extent here. That
        // somewhat blurs the lines between bounds inference and skip stages.
        merge_func_info(&old, func_info);
        old.swap(func_info);

        if (body.same_as(op->body)) {
            return op;
        } else {
            return For::make(op->name, op->min, op->max,
                             op->for_type, op->partition_policy, op->device_api, std::move(body));
        }
    }
};

// Just drop the skip stages marker in the IR. Used when we deduce that we don't
// need to run the mutator above.
class StripSkipStagesMarker : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::skip_stages_marker)) {
            return 0;
        } else {
            return op;
        }
    }
};

}  // namespace

Stmt skip_stages(const Stmt &stmt,
                 const std::vector<Function> &outputs,
                 const std::vector<std::vector<std::string>> &order,
                 const std::map<std::string, Function> &env) {

    // Each thing we might want to skip gets a unique id, sorted by realization
    // order of the corresponding Func.
    std::map<std::string, size_t> func_id;
    std::vector<std::string> name_for_id(order.size());
    for (size_t i = 0; i < order.size(); i++) {
        // Funcs in a compute_with group get the same id, because you can either
        // skip them all or skip none of them.
        for (const auto &f : order[i]) {
            func_id[f] = i;
        }
        name_for_id[i] = order[i][0];
    }

    // Map any .buffer symbols back to the id of the Func they refer to.
    for (const auto &p : env) {
        for (const auto &buf : p.second.output_buffers()) {
            func_id[buf.name() + ".buffer"] = func_id[p.first];
        }
    }

    // Make a map from Funcs to the first member of any compute_with group they belong to.
    SkipStagesAnalysis analysis(func_id);
    stmt.accept(&analysis);

    if (analysis.conditionally_used_funcs.empty()) {
        // Nothing to do. No Funcs can be skipped. Just strip the skip stages
        // marker.
        return StripSkipStagesMarker().mutate(stmt);
    }

    // There may be no calls to the output, which means they'll show up in
    // neither set. Add them to the unconditionally used set so that the mutator
    // knows to skip them.
    for (const Function &f : outputs) {
        analysis.unconditionally_used_funcs.insert(func_id[f.name()]);
    }

    return SkipStages(analysis, name_for_id).mutate(stmt);
}

}  // namespace Internal
}  // namespace Halide

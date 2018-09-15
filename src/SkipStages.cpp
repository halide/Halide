#include "SkipStages.h"
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

#include <iterator>

namespace Halide {
namespace Internal {

using std::map;
using std::set;
using std::string;
using std::vector;

namespace {

bool extern_call_uses_buffer(const Call *op, const std::string &func) {
    if (op->is_extern()) {
        if (starts_with(op->name, "halide_memoization")) {
            return false;
        }
        for (size_t i = 0; i < op->args.size(); i++) {
            const Variable *var = op->args[i].as<Variable>();
            if (var &&
                starts_with(var->name, func + ".") &&
                ends_with(var->name, ".buffer")) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

class PredicateFinder : public IRVisitor {
public:
    Expr predicate;
    PredicateFinder(const string &b, bool s) : predicate(const_false()),
                                               buffer(b),
                                               varies(false),
                                               treat_selects_as_guards(s),
                                               in_produce(false) {}

private:

    using IRVisitor::visit;
    string buffer;
    bool varies;
    bool treat_selects_as_guards;
    bool in_produce;
    Scope<> varying;
    Scope<> in_pipeline;
    Scope<> local_buffers;


    void visit(const Variable *op) {
        bool this_varies = varying.contains(op->name);

        varies |= this_varies;
    }

    void visit(const For *op) {
        op->min.accept(this);
        bool min_varies = varies;
        op->extent.accept(this);
        bool should_pop = false;
        if (!is_one(op->extent) || min_varies) {
            should_pop = true;
            varying.push(op->name);
        }
        op->body.accept(this);
        if (should_pop) {
            varying.pop(op->name);
            //internal_assert(!expr_uses_var(predicate, op->name));
        } else if (expr_uses_var(predicate, op->name)) {
            predicate = Let::make(op->name, op->min, predicate);
        }
    }

    template<typename T>
    void visit_let(const std::string &name, Expr value, T body) {
        bool old_varies = varies;
        varies = false;
        value.accept(this);
        bool value_varies = varies;
        varies |= old_varies;
        if (value_varies) {
            varying.push(name);
        }
        body.accept(this);
        if (value_varies) {
            varying.pop(name);
        }
        if (expr_uses_var(predicate, name)) {
            predicate = Let::make(name, value, predicate);
        }
    }

    void visit(const LetStmt *op) {
        visit_let(op->name, op->value, op->body);
    }

    void visit(const Let *op) {
        visit_let(op->name, op->value, op->body);
    }

    void visit(const ProducerConsumer *op) {
        ScopedBinding<> bind(in_pipeline, op->name);
        if (op->is_producer && op->name == buffer) {
            ScopedValue<bool> sv(in_produce, true);
            IRVisitor::visit(op);
        } else {
            IRVisitor::visit(op);
        }
    }

    // Logical operators with eager constant folding
    Expr make_and(Expr a, Expr b) {
        if (is_zero(a) || is_one(b)) {
            return a;
        } else if (is_zero(b) || is_one(a)) {
            return b;
        } else if (equal(a, b)) {
            return a;
        } else {
            return a && b;
        }
    }

    Expr make_or(Expr a, Expr b) {
        if (is_zero(a) || is_one(b)) {
            return b;
        } else if (is_zero(b) || is_one(a)) {
            return a;
        } else if (equal(a, b)) {
            return a;
        } else {
            return a || b;
        }
    }

    Expr make_select(Expr a, Expr b, Expr c) {
        if (is_one(a)) {
            return b;
        } else if (is_zero(a)) {
            return c;
        } else if (is_one(b)) {
            return make_or(a, c);
        } else if (is_zero(b)) {
            return make_and(make_not(a), c);
        } else if (is_one(c)) {
            return make_or(make_not(a), b);
        } else if (is_zero(c)) {
            return make_and(a, b);
        } else {
            return select(a, b, c);
        }
    }

    Expr make_not(Expr a) {
        if (is_one(a)) {
            return make_zero(a.type());
        } else if (is_zero(a)) {
            return make_one(a.type());
        } else {
            return !a;
        }
    }

    template<typename T>
    void visit_conditional(Expr condition, T true_case, T false_case) {
        Expr old_predicate = predicate;

        predicate = const_false();
        true_case.accept(this);
        Expr true_predicate = predicate;

        predicate = const_false();
        if (false_case.defined()) false_case.accept(this);
        Expr false_predicate = predicate;

        bool old_varies = varies;
        predicate = const_false();
        varies = false;
        condition.accept(this);

        predicate = make_or(predicate, old_predicate);
        if (varies) {
            predicate = make_or(predicate, make_or(true_predicate, false_predicate));
        } else {
            predicate = make_or(predicate, make_select(condition, true_predicate, false_predicate));
        }

        varies = varies || old_varies;
    }

    void visit(const Select *op) {
        if (treat_selects_as_guards) {
            visit_conditional(op->condition, op->true_value, op->false_value);
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const IfThenElse *op) {
        visit_conditional(op->condition, op->then_case, op->else_case);
    }

    void visit(const Call *op) {
        varies |= in_pipeline.contains(op->name);

        IRVisitor::visit(op);

        if (!in_produce && (op->name == buffer || extern_call_uses_buffer(op, buffer))) {
            predicate = const_true();
        }
    }

    void visit(const Provide *op) {
        IRVisitor::visit(op);
        if (in_produce && op->name != buffer && !local_buffers.contains(op->name)) {
            predicate = const_true();
        }
    }

    void visit(const Realize *op) {
        ScopedBinding<> bind(local_buffers, op->name);
        IRVisitor::visit(op);
    }

    void visit(const Allocate *op) {
        // This code works to ensure expressions depending on an
        // allocation don't get moved outside the allocation and are
        // marked as varying if predicate depends on the value of the
        // allocation.
        ScopedBinding<>
            bind_host_ptr(varying, op->name),
            bind_buffer(varying, op->name + ".buffer");
        IRVisitor::visit(op);
    }
};

class ProductionGuarder : public IRMutator2 {
public:
    ProductionGuarder(const string &b, Expr compute_p, Expr alloc_p):
        buffer(b), compute_predicate(compute_p), alloc_predicate(alloc_p) {}
private:
    string buffer;
    Expr compute_predicate;
    Expr alloc_predicate;

    using IRMutator2::visit;

    bool memoize_call_uses_buffer(const Call *op) {
        internal_assert(op->call_type == Call::Extern);
        internal_assert(starts_with(op->name, "halide_memoization"));
        for (size_t i = 0; i < op->args.size(); i++) {
            const Variable *var = op->args[i].as<Variable>();
            if (var &&
                starts_with(var->name, buffer + ".") &&
                ends_with(var->name, ".buffer")) {
                return true;
            }
        }
        return false;
    }

    Expr visit(const Call *op) override {

        if ((op->name == "halide_memoization_cache_lookup") &&
             memoize_call_uses_buffer(op)) {
            // We need to guard call to halide_memoization_cache_lookup to only
            // be executed if the corresponding buffer is allocated. We ignore
            // the compute_predicate since in the case that alloc_predicate is
            // true but compute_predicate is false, the consumer would still load
            // data from the buffer even if it won't actually use the result,
            // hence, we need to allocate some scratch memory for the consumer
            // to load from. For memoized func, the memory might already be in
            // the cache, so we perform the lookup instead of allocating a new one.
            return Call::make(op->type, Call::if_then_else,
                              {alloc_predicate, op, 0}, Call::PureIntrinsic);
        } else if ((op->name == "halide_memoization_cache_store") &&
                    memoize_call_uses_buffer(op)) {
            // We need to wrap the halide_memoization_cache_store with the
            // compute_predicate, since the data to be written is only valid if
            // the producer of the buffer is executed.
            return Call::make(op->type, Call::if_then_else,
                              {compute_predicate, op, 0}, Call::PureIntrinsic);
        } else {
            return IRMutator2::visit(op);
        }
    }

    Stmt visit(const ProducerConsumer *op) override {
        // If the compute_predicate at this stage depends on something
        // vectorized we should bail out.
        Stmt stmt = IRMutator2::visit(op);

        if (op->is_producer) {
            op = stmt.as<ProducerConsumer>();
            internal_assert(op);
            if (op->name == buffer) {
                Stmt body = IfThenElse::make(compute_predicate, op->body);
                stmt = ProducerConsumer::make(op->name, op->is_producer, body);
            }
        }
        return stmt;
    }
};

class StageSkipper : public IRMutator2 {
public:
    StageSkipper(const string &f) : func(f), in_vector_loop(false) {}
private:
    string func;
    using IRMutator2::visit;

    Scope<> vector_vars;
    bool in_vector_loop;

    Stmt visit(const For *op) override {
        bool old_in_vector_loop = in_vector_loop;

        // We want to be sure that the predicate doesn't vectorize.
        if (op->for_type == ForType::Vectorized) {
            vector_vars.push(op->name);
            in_vector_loop = true;
        }

        Stmt stmt = IRMutator2::visit(op);

        if (op->for_type == ForType::Vectorized) {
            vector_vars.pop(op->name);
        }

        in_vector_loop = old_in_vector_loop;

        return stmt;
    }

    Stmt visit(const LetStmt *op) override {
        struct Frame {
            const LetStmt *op;
            bool vector_var;
        };
        vector<Frame> frames;
        Stmt result;

        while (op) {
            bool vector_var = in_vector_loop && expr_uses_vars(op->value, vector_vars);
            frames.push_back({op, vector_var});
            if (vector_var) {
                vector_vars.push(op->name);
            }
            result = op->body;
            op = result.as<LetStmt>();
        }

        result = mutate(result);

        for (auto it = frames.rbegin(); it != frames.rend(); it++) {
            if (it->vector_var) {
                vector_vars.pop(it->op->name);
            }
            result = LetStmt::make(it->op->name, it->op->value, result);
        }
        return result;
    }

    Stmt visit(const Realize *op) override {
        if (op->name == func) {
            debug(3) << "Finding compute predicate for " << op->name << "\n";
            PredicateFinder find_compute(op->name, true);
            op->body.accept(&find_compute);

            debug(3) << "Simplifying compute predicate for " << op->name << ": " << find_compute.predicate << "\n";
            Expr compute_predicate = simplify(common_subexpression_elimination(find_compute.predicate));

            debug(3) << "Compute predicate for " << op->name << " : " << compute_predicate << "\n";

            if (expr_uses_vars(compute_predicate, vector_vars)) {
                // Don't try to skip stages if the predicate may vary
                // per lane. This will just unvectorize the
                // production, which is probably contrary to the
                // intent of the user.
                compute_predicate = const_true();
            }

            if (!is_one(compute_predicate)) {

                debug(3) << "Finding allocate predicate for " << op->name << "\n";
                PredicateFinder find_alloc(op->name, false);
                op->body.accept(&find_alloc);
                debug(3) << "Simplifying allocate predicate for " << op->name << "\n";
                Expr alloc_predicate = simplify(common_subexpression_elimination(find_alloc.predicate));

                debug(3) << "Allocate predicate for " << op->name << " : " << alloc_predicate << "\n";

                ProductionGuarder g(op->name, compute_predicate, alloc_predicate);
                Stmt body = g.mutate(op->body);

                debug(3) << "Done guarding computation for " << op->name << "\n";

                return Realize::make(op->name, op->types, op->memory_type, op->bounds,
                                     alloc_predicate, body);
            } else {
                return IRMutator2::visit(op);
            }
        } else {
            return IRMutator2::visit(op);
        }
    }
};

// Find Funcs where at least one of the consume nodes only uses the
// Func conditionally. We may want to guard the production of these
// Funcs.
class MightBeSkippable : public IRVisitor {

    using IRVisitor::visit;

    bool in_conditional_stmt{false};

    void visit(const Call *op) {
        IRVisitor::visit(op);
        if (op->call_type == Call::Halide) {
            unconditionally_used.insert(op->name);
        }
    }

    void visit(const IfThenElse *op) {
        op->condition.accept(this);

        std::set<string> old;
        unconditionally_used.swap(old);

        ScopedValue<bool> old_in_conditional(in_conditional_stmt, true);
        op->then_case.accept(this);

        std::set<string> used_in_true;
        used_in_true.swap(unconditionally_used);
        if (op->else_case.defined()) {
            op->else_case.accept(this);
        }

        // Take the set intersection of the true and false paths, and add them to the set.
        std::set_intersection(used_in_true.begin(), used_in_true.end(),
                              unconditionally_used.begin(), unconditionally_used.end(),
                              std::inserter(old, old.begin()));

        unconditionally_used.swap(old);
    }

    void visit(const Select *op) {
        op->condition.accept(this);

        std::set<string> old;
        unconditionally_used.swap(old);

        op->true_value.accept(this);
        std::set<string> used_in_true;
        used_in_true.swap(unconditionally_used);

        op->false_value.accept(this);

        // Again, take the set intersection
        std::set_intersection(used_in_true.begin(), used_in_true.end(),
                              unconditionally_used.begin(), unconditionally_used.end(),
                              std::inserter(old, old.begin()));

        unconditionally_used.swap(old);
    }

    void visit(const ProducerConsumer *op) {
        if (!op->is_producer) {
            op->body.accept(this);
            if (!unconditionally_used.count(op->name) || in_conditional_stmt) {
                // This Func has a least one consume clause in which
                // it is only used conditionally.
                candidates.insert(op->name);
            }
        } else {
            IRVisitor::visit(op);
            // Calls inside the produce don't count - that's the block of code we intend to guard.
            unconditionally_used.erase(op->name);
        }
    }

    set<string> unconditionally_used;
public:
    set<string> candidates;
};

Stmt skip_stages(Stmt stmt, const vector<string> &order) {
    // Don't consider the last stage, because it's the output, so it's
    // never skippable.
    MightBeSkippable check;
    stmt.accept(&check);
    for (size_t i = order.size()-1; i > 0; i--) {
        debug(2) << "skip_stages checking " << order[i-1] << "\n";
        if (check.candidates.count(order[i-1])) {
            debug(2) << "skip_stages can skip " << order[i-1] << "\n";
            StageSkipper skipper(order[i-1]);
            Stmt new_stmt = skipper.mutate(stmt);
            if (!new_stmt.same_as(stmt)) {
                // Might have made earlier stages skippable too
                new_stmt.accept(&check);
            }
            stmt = new_stmt;
        }
    }
    return stmt;
}

}  // namespace Internal
}  // namespace Halide

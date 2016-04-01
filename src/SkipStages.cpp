#include "SkipStages.h"
#include "Debug.h"
#include "IRMutator.h"
#include "IRPrinter.h"
#include "IROperator.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"
#include "ExprUsesVar.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::map;

namespace {

bool extern_call_uses_buffer(const Call *op, const std::string &func) {
    if (op->call_type == Call::Extern ||
        op->call_type == Call::ExternCPlusPlus) {
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

}


class PredicateFinder : public IRVisitor {
public:
    Expr predicate;
    PredicateFinder(const string &b, bool s) : predicate(const_false()),
                                               buffer(b),
                                               varies(false),
                                               treat_selects_as_guards(s) {}

private:

    using IRVisitor::visit;
    string buffer;
    bool varies;
    bool treat_selects_as_guards;
    Scope<int> varying;
    Scope<int> in_pipeline;

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
            varying.push(op->name, 0);
        }
        op->body.accept(this);
        if (should_pop) {
            varying.pop(op->name);
            //internal_assert(!expr_uses_var(predicate, op->name));
        } else {
            predicate = substitute(op->name, op->min, predicate);
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
            varying.push(name, 0);
        }
        body.accept(this);
        if (value_varies) {
            varying.pop(name);
        }
        predicate = substitute(name, value, predicate);
    }

    void visit(const LetStmt *op) {
        visit_let(op->name, op->value, op->body);
    }

    void visit(const Let *op) {
        visit_let(op->name, op->value, op->body);
    }

    void visit(const ProducerConsumer *op) {
        in_pipeline.push(op->name, 0);
        if (op->name != buffer) {
            op->produce.accept(this);
            if (op->update.defined()) {
                op->update.accept(this);
            }
        }
        op->consume.accept(this);
        in_pipeline.pop(op->name);
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

        if (is_one(predicate) || is_one(old_predicate)) {
            predicate = const_true();
        } else if (varies) {
            if (is_one(true_predicate) || is_one(false_predicate)) {
                predicate = const_true();
            } else {
                predicate = (old_predicate || predicate ||
                             true_predicate || false_predicate);
            }
        } else {
            predicate = (old_predicate || predicate ||
                         (condition && true_predicate) ||
                         ((!condition) && false_predicate));
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
        // Calls inside of an address_of aren't considered, because no
        // actuall call to the Func happens.
        if (op->is_intrinsic(Call::address_of)) {
            // Visit the args of the inner call
            const Call *c = op->args[0].as<Call>();
            if (c) {
                varies |= varying.contains(c->name);
                for (size_t i = 0; i < c->args.size(); i++) {
                    c->args[i].accept(this);
                }
            } else {
                const Load *l = op->args[0].as<Load>();

                internal_assert(l);
                varies |= varying.contains(l->name);
                l->index.accept(this);
            }
            return;
        }

        varies |= in_pipeline.contains(op->name);

        IRVisitor::visit(op);

        if (op->name == buffer || extern_call_uses_buffer(op, buffer)) {
            predicate = const_true();
        }
    }

    void visit(const Allocate *op) {
        // This code works to ensure expressions depending on an
        // allocation don't get moved outside the allocation and are
        // marked as varying if predicate depends on the value of the
        // allocation.
        varying.push(op->name, 0);
        varying.push(op->name + ".buffer", 0);
        varying.push(op->name + ".host", 0);
        IRVisitor::visit(op);
        varying.pop(op->name + ".host");
        varying.pop(op->name + ".buffer");
        varying.pop(op->name);
    }
};

class ProductionGuarder : public IRMutator {
public:
    ProductionGuarder(const string &b, Expr compute_p, Expr alloc_p):
        buffer(b), compute_predicate(compute_p), alloc_predicate(alloc_p) {}
private:
    string buffer;
    Expr compute_predicate;
    Expr alloc_predicate;

    using IRMutator::visit;

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

    void visit(const Call *op) {

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
            expr = Call::make(op->type, Call::if_then_else,
                              {alloc_predicate, op, 0}, Call::PureIntrinsic);
        } else if ((op->name == "halide_memoization_cache_store") &&
                    memoize_call_uses_buffer(op)) {
            // We need to wrap the halide_memoization_cache_store with the
            // compute_predicate, since the data to be written is only valid if
            // the producer of the buffer is executed.
            expr = Call::make(op->type, Call::if_then_else,
                              {compute_predicate, op, 0}, Call::PureIntrinsic);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const ProducerConsumer *op) {
        // If the compute_predicate at this stage depends on something
        // vectorized we should bail out.
        IRMutator::visit(op);

        op = stmt.as<ProducerConsumer>();
        internal_assert(op);
        if (op->name == buffer) {
            Stmt produce = op->produce, update = op->update;
            if (update.defined()) {
                Expr predicate_var = Variable::make(Bool(), buffer + ".needed");
                Stmt produce = IfThenElse::make(predicate_var, op->produce);
                Stmt update = IfThenElse::make(predicate_var, op->update);
                stmt = ProducerConsumer::make(op->name, produce, update, op->consume);
                stmt = LetStmt::make(buffer + ".needed", compute_predicate, stmt);
            } else {
                Stmt produce = IfThenElse::make(compute_predicate, op->produce);
                stmt = ProducerConsumer::make(op->name, produce, Stmt(), op->consume);
            }

        }
    }
};

class StageSkipper : public IRMutator {
public:
    StageSkipper(const string &f) : func(f), in_vector_loop(false) {}
private:
    string func;
    using IRMutator::visit;

    Scope<int> vector_vars;
    bool in_vector_loop;

    void visit(const For *op) {
        bool old_in_vector_loop = in_vector_loop;

        // We want to be sure that the predicate doesn't vectorize.
        if (op->for_type == ForType::Vectorized) {
            vector_vars.push(op->name, 0);
            in_vector_loop = true;
        }

        IRMutator::visit(op);

        if (op->for_type == ForType::Vectorized) {
            vector_vars.pop(op->name);
        }

        in_vector_loop = old_in_vector_loop;
    }

    void visit(const LetStmt *op) {
        bool should_pop = false;
        if (in_vector_loop &&
            expr_uses_vars(op->value, vector_vars)) {
            should_pop = true;
            vector_vars.push(op->name, 0);
        }

        IRMutator::visit(op);

        if (should_pop) {
            vector_vars.pop(op->name);
        }
    }

    void visit(const Realize *op) {
        if (op->name == func) {
            PredicateFinder find_compute(op->name, true);
            op->body.accept(&find_compute);
            Expr compute_predicate = simplify(find_compute.predicate);

            if (expr_uses_vars(compute_predicate, vector_vars)) {
                // Don't try to skip stages if the predicate may vary
                // per lane. This will just unvectorize the
                // production, which is probably contrary to the
                // intent of the user.
                compute_predicate = const_true();
            }

            if (!is_one(compute_predicate)) {
                PredicateFinder find_alloc(op->name, false);
                op->body.accept(&find_alloc);
                Expr alloc_predicate = simplify(find_alloc.predicate);

                ProductionGuarder g(op->name, compute_predicate, alloc_predicate);
                Stmt body = g.mutate(op->body);

                stmt = Realize::make(op->name, op->types, op->bounds,
                                     alloc_predicate, body);
            } else {
                IRMutator::visit(op);
            }
        } else {
            IRMutator::visit(op);
        }
    }
};

// Check if all calls to a given function are behind an if of some
// sort (but don't worry about what it is).
class MightBeSkippable : public IRVisitor {

    using IRVisitor::visit;

    void visit(const Call *op) {
        // Calls inside of an address_of aren't considered, because no
        // actuall call to the Func happens.
        if (op->is_intrinsic(Call::address_of)) {
            // Visit the args of the inner call
            internal_assert(op->args.size() == 1);
            const Call *c = op->args[0].as<Call>();
            if (c) {
                for (size_t i = 0; i < c->args.size(); i++) {
                    c->args[i].accept(this);
                }
            } else {
                const Load *l = op->args[0].as<Load>();

                internal_assert(l);
                l->index.accept(this);
            }
            return;
        }
        IRVisitor::visit(op);
        if (op->name == func || extern_call_uses_buffer(op, func)) {
            result &= guarded;
        }
    }

    void visit(const IfThenElse *op) {
        op->condition.accept(this);

        bool old = guarded;
        guarded = true;

        op->then_case.accept(this);
        if (op->else_case.defined()) {
            op->else_case.accept(this);
        }

        guarded = old;
    }

    void visit(const Select *op) {
        op->condition.accept(this);

        bool old = guarded;
        guarded = true;

        op->true_value.accept(this);
        op->false_value.accept(this);

        guarded = old;
    }

    void visit(const Realize *op) {
        if (op->name == func) {
            guarded = false;
        }
        IRVisitor::visit(op);
    }

    void visit(const ProducerConsumer *op) {
        if (op->name == func) {
            bool old_result = result;
            result = true;
            op->consume.accept(this);
            result = result || old_result;
        } else {
            IRVisitor::visit(op);
        }
    }

    string func;
    bool guarded;

public:
    bool result;

    MightBeSkippable(string f) : func(f), guarded(false), result(false) {}
};

Stmt skip_stages(Stmt stmt, const vector<string> &order) {
    // Don't consider the last stage, because it's the output, so it's
    // never skippable.
    for (size_t i = order.size()-1; i > 0; i--) {
        debug(2) << "skip_stages checking " << order[i-1] << "\n";
        MightBeSkippable check(order[i-1]);
        stmt.accept(&check);
        if (check.result) {
            debug(2) << "skip_stages can skip " << order[i-1] << "\n";
            StageSkipper skipper(order[i-1]);
            stmt = skipper.mutate(stmt);
        }
    }
    return stmt;
}

}
}

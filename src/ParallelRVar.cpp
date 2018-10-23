#include "ParallelRVar.h"
#include "CSE.h"
#include "Debug.h"
#include "IR.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

namespace {
/** Find all calls arguments to the given function. Substitutes in
 * lets, so take care with the combinatorially large results. */
class FindLoads : public IRVisitor {
    using IRVisitor::visit;

    const string &func;

    void visit(const Call *op) override {
        if (op->name == func && op->call_type == Call::Halide) {
            loads.push_back(op->args);
        }
        IRVisitor::visit(op);
    }

    void visit(const Let *op) override {
        IRVisitor::visit(op);
        for (size_t i = 0; i < loads.size(); i++) {
            for (size_t j = 0; j < loads[i].size(); j++) {
                loads[i][j] = substitute(op->name, op->value, loads[i][j]);
            }
        }
    }

public:
    FindLoads(const string &f) : func(f) {}

    vector<vector<Expr>> loads;
};

/** Rename all free variables to unique new names. */
class RenameFreeVars : public IRMutator2 {
    using IRMutator2::visit;

    map<string, string> new_names;

    Expr visit(const Variable *op) override {
        if (!op->param.defined() && !op->image.defined()) {
            return Variable::make(op->type, get_new_name(op->name));
        } else {
            return op;
        }
    }

public:
    string get_new_name(const string &s) {
        map<string, string>::iterator iter = new_names.find(s);
        if (iter != new_names.end()) {
            return iter->second;
        } else {
            string new_name = s + "$_";
            new_names[s] = new_name;
            return new_name;
        }
    }
};

/** Substitute in boolean expressions. */
class SubstituteInBooleanLets : public IRMutator2 {
    using IRMutator2::visit;

    Expr visit(const Let *op) override {
        if (op->value.type() == Bool()) {
            return substitute(op->name, mutate(op->value), mutate(op->body));
        } else {
            return IRMutator2::visit(op);
        }
    }
};
}  // namespace

bool can_parallelize_rvar(const string &v,
                          const string &f,
                          const Definition &r) {
    const vector<Expr> &values = r.values();
    const vector<Expr> &args = r.args();
    const vector<ReductionVariable> &rvars = r.schedule().rvars();

    FindLoads find(f);
    for (size_t i = 0; i < values.size(); i++) {
        values[i].accept(&find);
    }

    // Make an expr representing the store done by a different thread.
    RenameFreeVars renamer;
    vector<Expr> other_store(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        other_store[i] = renamer.mutate(args[i]);
    }

    // Construct an expression which is true when the two threads are
    // in fact two different threads. We'll use this liberally in the
    // following conditions to give the simplifier the best chance.
    Expr distinct_v = (Variable::make(Int(32), v) !=
                       Variable::make(Int(32), renamer.get_new_name(v)));

    // Construct an expression which is true if there's a collision
    // between this thread's store and the other thread's store.
    Expr hazard = const_true();
    for (size_t i = 0; i < args.size(); i++) {
        hazard = hazard && (distinct_v && (args[i] == other_store[i]));
    }

    // Add expressions that are true if there's a collision between
    // the other thread's store and this thread's loads.
    for (size_t i = 0; i < find.loads.size(); i++) {
        internal_assert(find.loads[i].size() == other_store.size());
        Expr check = const_true();
        for (size_t j = 0; j < find.loads[i].size(); j++) {
            check = check && (distinct_v && (find.loads[i][j] == other_store[j]));
        }
        hazard = hazard || check;
    }

    // Make a scope representing the bounds of the reduction domain
    Scope<Interval> bounds;
    for (const auto &rv : rvars) {
        Interval in = Interval(rv.min, simplify(rv.min + rv.extent - 1));
        bounds.push(rv.var, in);
        bounds.push(renamer.get_new_name(rv.var), in);
    }

    // Add the definition's predicate if there is any
    Expr pred = simplify(r.predicate());
    if (pred.defined() || !equal(const_true(), pred)) {
        Expr this_pred = pred;
        Expr other_pred = renamer.mutate(pred);
        debug(3) << "......this thread predicate: " << this_pred << "\n";
        debug(3) << "......other thread predicate: " << other_pred << "\n";
        hazard = hazard && this_pred && other_pred;
    }

    debug(3) << "Attempting to falsify: " << hazard << "\n";
    // Pull out common non-boolean terms
    hazard = common_subexpression_elimination(hazard);
    hazard = SubstituteInBooleanLets().mutate(hazard);
    hazard = simplify(hazard, false, bounds);
    debug(3) << "Simplified to: " << hazard << "\n";

    // strip lets
    while (const Let *l = hazard.as<Let>()) {
        hazard = l->body;
    }

    return is_zero(hazard);
}

}  // namespace Internal
}  // namespace Halide

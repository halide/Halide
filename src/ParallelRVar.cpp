#include "IR.h"
#include "ParallelRVar.h"
#include "IRMutator.h"
#include "Debug.h"
#include "Simplify.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::map;

/** Find all calls arguments to the given function. Note that we don't
 * pick up lets for simplicity. This makes the comparison
 * conservative, because the let variables become unknowns. */
class FindLoads : public IRVisitor {
    using IRVisitor::visit;

    const string &func;

    void visit(const Call *op) {
        if (op->name == func && op->call_type == Call::Halide) {
            loads.push_back(op->args);
        }
        IRVisitor::visit(op);
    }

public:
    FindLoads(const string &f) : func(f) {}

    vector<vector<Expr> > loads;
};

/** Rename all free variables to unique new names. */
class RenameFreeVars : public IRMutator {
    using IRMutator::visit;

    map<string, string> new_names;

    void visit(const Variable *op) {
        if (!op->param.defined() && !op->image.defined()) {
            expr = Variable::make(op->type, get_new_name(op->name));
        } else {
            expr = op;
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

bool can_parallelize_rvar(const string &v,
                          const string &f,
                          const UpdateDefinition &r) {
    FindLoads find(f);
    for (size_t i = 0; i < r.values.size(); i++) {
        r.values[i].accept(&find);
    }

    // Make an expr representing the store done by a different thread.
    RenameFreeVars renamer;
    vector<Expr> other_store(r.args.size());
    for (size_t i = 0; i < r.args.size(); i++) {
        other_store[i] = renamer.mutate(r.args[i]);
    }

    // Construct an expression which is true when the two threads are
    // in fact two different threads. We'll use this liberally in the
    // following conditions to give the simplifier the best chance.
    Expr distinct_v = (Variable::make(Int(32), v) !=
                       Variable::make(Int(32), renamer.get_new_name(v)));

    // Construct an expression which is true if there's a collision
    // between this thread's store and the other thread's store.
    Expr hazard = const_true();
    for (size_t i = 0; i < r.args.size(); i++) {
        hazard = hazard && (distinct_v && (r.args[i] == other_store[i]));
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
    if (r.domain.defined()) {
        for (size_t i = 0; i < r.domain.domain().size(); i++) {
            const ReductionVariable &rv = r.domain.domain()[i];
            Interval in = Interval(rv.min, simplify(rv.min + rv.extent - 1));
            bounds.push(rv.var, in);
            bounds.push(renamer.get_new_name(rv.var), in);
        }
    }

    debug(3) << "Attempting to falsify: " << hazard << "\n";
    hazard = simplify(hazard, false, bounds);
    debug(3) << "Simplified to: " << hazard << "\n";

    return is_zero(hazard);
}

}
}

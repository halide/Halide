#include "LICM.h"
#include "CSE.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::vector;
using std::pair;
using std::set;

// Is it safe to lift an Expr out of a loop (and potentially across a device boundary)
class CanLift : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *op) {
        if (!op->is_pure()) {
            result = false;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Load *op) {
        result = false;
    }

    void visit(const Variable *op) {
        if (varying.contains(op->name)) {
            result = false;
        }
    }

    const Scope<int> &varying;

public:
    bool result {true};

    CanLift(const Scope<int> &v) : varying(v) {}
};

// Lift pure loop invariants to the top level. Applied independently
// to each loop.
class LiftLoopInvariants : public IRMutator2 {
    using IRMutator2::visit;

    Scope<int> varying;

    bool can_lift(const Expr &e) {
        CanLift check(varying);
        e.accept(&check);
        return check.result;
    }

    bool should_lift(const Expr &e) {
        if (!can_lift(e)) return false;
        if (e.as<Variable>()) return false;
        if (e.as<Broadcast>()) return false;
        if (is_const(e)) return false;
        // bool vectors are buggy enough in LLVM that lifting them is a bad idea.
        // (We just skip all vectors on the principle that we don't want them
        // on the stack anyway.)
        if (e.type().is_vector()) return false;
        if (const Cast *cast = e.as<Cast>()) {
            if (cast->type.bytes() > cast->value.type().bytes()) {
                // Don't lift widening casts.
                return false;
            }
        }
        return true;
    }

    Expr visit(const Let *op) override {
        ScopedBinding<int> p(varying, op->name, 0);
        return IRMutator2::visit(op);
    }

    Stmt visit(const LetStmt *op) override {
        ScopedBinding<int> p(varying, op->name, 0);
        return IRMutator2::visit(op);
    }

    Stmt visit(const For *op) override {
        ScopedBinding<int> p(varying, op->name, 0);
        return IRMutator2::visit(op);
    }

public:

    using IRMutator2::mutate;

    Expr mutate(const Expr &e) override {
        if (should_lift(e)) {
            // Lift it in canonical form
            Expr lifted_expr = simplify(e);
            auto it = lifted.find(lifted_expr);
            if (it == lifted.end()) {
                string name = unique_name('t');
                lifted[lifted_expr] = name;
                return Variable::make(e.type(), name);
            } else {
                return Variable::make(e.type(), it->second);
            }
        } else {
            return IRMutator2::mutate(e);
        }
    }

    map<Expr, string, IRDeepCompare> lifted;
};

class LICM : public IRMutator2 {
    using IRMutator2::visit;

    bool in_gpu_loop{false};

    // Compute the cost of computing an expression inside the inner
    // loop, compared to just loading it as a parameter.
    int cost(const Expr &e, const set<string> &vars) {
        if (is_const(e)) {
            return 0;
        } else if (const Variable *var = e.as<Variable>()) {
            if (vars.count(var->name)) {
                // We're loading this already
                return 0;
            } else {
                // Would have to load this
                return 1;
            }
        } else if (const Add *add = e.as<Add>()) {
            return cost(add->a, vars) + cost(add->b, vars) + 1;
        } else if (const Sub *sub = e.as<Sub>()) {
            return cost(sub->a, vars) + cost(sub->b, vars) + 1;
        } else if (const Mul *mul = e.as<Mul>()) {
            return cost(mul->a, vars) + cost(mul->b, vars) + 1;
        } else {
            return 100;
        }
    }

    Stmt visit(const For *op) override {
        ScopedValue<bool> old_in_gpu_loop(in_gpu_loop);
        in_gpu_loop =
            (op->for_type == ForType::GPUBlock ||
             op->for_type == ForType::GPUThread);

        if (old_in_gpu_loop && in_gpu_loop) {
            // Don't lift lets to in-between gpu blocks/threads
            return IRMutator2::visit(op);
        } else if (op->device_api == DeviceAPI::GLSL ||
                   op->device_api == DeviceAPI::OpenGLCompute) {
            // Don't lift anything out of OpenGL loops
            return IRMutator2::visit(op);
        } else {

            // Lift invariants
            LiftLoopInvariants lifter;
            Stmt new_stmt = lifter.mutate(op);

            // As an optimization to reduce register pressure, take
            // the set of expressions to lift and check if any can
            // cheaply be computed from others. If so it's better to
            // do that than to load multiple related values off the
            // stack. We currently only consider expressions that are
            // the sum, difference, or product of two variables
            // already used in the kernel, or a variable plus a
            // constant.

            // Linearize all the exprs and names
            vector<Expr> exprs;
            vector<string> names;
            for (const auto &p : lifter.lifted) {
                exprs.push_back(p.first);
                names.push_back(p.second);
            }

            // Jointly CSE the lifted exprs put putting them together into a dummy Expr
            Expr dummy_call = Call::make(Int(32), "dummy", exprs, Call::Extern);
            dummy_call = common_subexpression_elimination(dummy_call, true);

            // Peel off containing lets. These will be lifted.
            vector<pair<string, Expr>> lets;
            while (const Let *let = dummy_call.as<Let>()) {
                lets.push_back({let->name, let->value});
                dummy_call = let->body;
            }

            // Track the set of variables used by the inner loop
            class CollectVars : public IRVisitor {
                using IRVisitor::visit;
                void visit(const Variable *op) {
                    vars.insert(op->name);
                }
            public:
                set<string> vars;
            } vars;
            new_stmt.accept(&vars);

            // Now consider substituting back in each use
            const Call *call = dummy_call.as<Call>();
            internal_assert(call);
            bool converged;
            do {
                converged = true;
                for (size_t i = 0; i < exprs.size(); i++) {
                    if (!exprs[i].defined()) continue;
                    Expr e = call->args[i];
                    if (cost(e, vars.vars) <= 1) {
                        // Just subs it back in - computing it is as cheap
                        // as loading it.
                        e.accept(&vars);
                        new_stmt = substitute(names[i], e, new_stmt);
                        names[i].clear();
                        exprs[i] = Expr();
                        converged = false;
                    } else {
                        exprs[i] = e;
                    }
                }
            } while (!converged);

            // Recurse
            const For *loop = new_stmt.as<For>();
            internal_assert(loop);

            new_stmt = For::make(loop->name, loop->min, loop->extent,
                                 loop->for_type, loop->device_api, mutate(loop->body));

            // Wrap lets for the lifted invariants
            for (size_t i = 0; i < exprs.size(); i++) {
                if (exprs[i].defined()) {
                    new_stmt = LetStmt::make(names[i], exprs[i], new_stmt);
                }
            }

            // Wrap the lets pulled out by CSE
            while (!lets.empty()) {
                new_stmt = LetStmt::make(lets.back().first, lets.back().second, new_stmt);
                lets.pop_back();
            }

            return new_stmt;
        }
    }
};


// Reassociate summations to group together the loop invariants. Useful to run before LICM.
class GroupLoopInvariants : public IRMutator2 {
    using IRMutator2::visit;

    Scope<int> var_depth;

    class ExprDepth : public IRVisitor {
        using IRVisitor::visit;
        const Scope<int> &depth;

        void visit(const Variable *op) {
            if (depth.contains(op->name)) {
                result = std::max(result, depth.get(op->name));
            }
        }
    public:
        int result = 0;
        ExprDepth(const Scope<int> &var_depth) : depth(var_depth) {}
    };

    int expr_depth(const Expr &e) {
        ExprDepth depth(var_depth);
        e.accept(&depth);
        return depth.result;
    }

    struct Term {
        Expr expr;
        bool positive;
        int depth;
    };

    vector<Term> extract_summation(const Expr &e) {
        vector<Term> pending, terms;
        pending.push_back({e, true, 0});
        while (!pending.empty()) {
            Term next = pending.back();
            pending.pop_back();
            const Add *add = next.expr.as<Add>();
            const Sub *sub = next.expr.as<Sub>();
            if (add) {
                pending.push_back({add->a, next.positive, 0});
                pending.push_back({add->b, next.positive, 0});
            } else if (sub) {
                pending.push_back({sub->a, next.positive, 0});
                pending.push_back({sub->b, !next.positive, 0});
            } else {
                next.expr = mutate(next.expr);
                if (next.expr.as<Add>() || next.expr.as<Sub>()) {
                    // After mutation it became an add or sub, throw it back on the pending queue.
                    pending.push_back(next);
                } else {
                    next.depth = expr_depth(next.expr);
                    terms.push_back(next);
                }
            }
        }

        // Sort the terms by loop depth. Terms of equal depth are
        // likely already in a good order, so don't mess with them.
        std::stable_sort(terms.begin(), terms.end(),
                  [](const Term &a, const Term &b) {
                      return a.depth > b.depth;
                  });

        return terms;
    }

    Expr reassociate_summation(const Expr &e) {
        vector<Term> terms = extract_summation(e);

        Expr result;
        bool positive = true;
        while (!terms.empty()) {
            Term next = terms.back();
            terms.pop_back();
            if (result.defined()) {
                if (next.positive == positive) {
                    result += next.expr;
                } else if (next.positive) {
                    result = next.expr - result;
                    positive = true;
                } else {
                    result -= next.expr;
                }
            } else {
                result = next.expr;
                positive = next.positive;
            }
        }

        if (!positive) {
            result = make_zero(result.type()) - result;
        }

        return result;
    }

    Expr visit(const Sub *op) override {
        if (op->type.is_float()) {
            // Don't reassociate float exprs.
            // (If strict_float is off, we're allowed to reassociate,
            // and we do reassociate elsewhere, but there's no benefit to it
            // here and it's friendlier not to.)
            return IRMutator2::visit(op);
        }

        return reassociate_summation(op);
    }

    Expr visit(const Add *op) override {
        if (op->type.is_float()) {
            // Don't reassociate float exprs.
            // (If strict_float is off, we're allowed to reassociate,
            // and we do reassociate elsewhere, but there's no benefit to it
            // here and it's friendlier not to.)
            return IRMutator2::visit(op);
        }

        return reassociate_summation(op);
    }

    int depth = 0;

    Stmt visit(const For *op) override {
        depth++;
        ScopedBinding<int> bind(var_depth, op->name, depth);
        Stmt stmt = IRMutator2::visit(op);
        depth--;
        return stmt;
    }

    Expr visit(const Let *op) override {
        ScopedBinding<int> bind(var_depth, op->name, expr_depth(op->value));
        return IRMutator2::visit(op);
    }

    Stmt visit(const LetStmt *op) override {
        ScopedBinding<int> bind(var_depth, op->name, expr_depth(op->value));
        return IRMutator2::visit(op);
    }

};

// Turn for loops of size one into let statements
Stmt loop_invariant_code_motion(Stmt s) {
    s = GroupLoopInvariants().mutate(s);
    s = common_subexpression_elimination(s);
    s = LICM().mutate(s);
    s = simplify_exprs(s);
    return s;
}

}
}

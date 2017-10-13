#include "LICM.h"
#include "CSE.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::vector;
using std::pair;

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
class LiftLoopInvariants : public IRMutator {
    using IRMutator::visit;

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
        return true;
    }

    void visit(const Let *op) {
        varying.push(op->name, 0);
        IRMutator::visit(op);
        varying.pop(op->name);
    }

    void visit(const LetStmt *op) {
        varying.push(op->name, 0);
        IRMutator::visit(op);
        varying.pop(op->name);
    }

    void visit(const For *op) {
        varying.push(op->name, 0);
        IRMutator::visit(op);
        varying.pop(op->name);
    }

public:

    using IRMutator::mutate;

    Expr mutate(const Expr &e) {
        if (should_lift(e)) {
            auto it = lifted.find(e);
            if (it == lifted.end()) {
                string name = unique_name('t');
                lifted[e] = name;
                return Variable::make(e.type(), name);
            } else {
                return Variable::make(e.type(), it->second);
            }
        } else {
            return IRMutator::mutate(e);
        }
    }

    map<Expr, string, IRDeepCompare> lifted;
};

class LICM : public IRMutator {
    using IRVisitor::visit;

    bool in_gpu_loop {false};

    void visit(const For *op) {
        bool old_in_gpu_loop = in_gpu_loop;
        in_gpu_loop =
            (op->for_type == ForType::GPUBlock ||
             op->for_type == ForType::GPUThread);

        if (old_in_gpu_loop && in_gpu_loop) {
            // Don't lift lets to in-between gpu blocks/threads
            IRMutator::visit(op);
        } else if (op->device_api == DeviceAPI::GLSL ||
                   op->device_api == DeviceAPI::OpenGLCompute) {
            // Don't lift anything out of OpenGL loops
            IRMutator::visit(op);
        } else {

            // Lift invariants
            LiftLoopInvariants lifter;
            Stmt new_stmt = lifter.mutate(op);

            // Recurse
            const For *loop = new_stmt.as<For>();
            internal_assert(loop);

            new_stmt = For::make(loop->name, loop->min, loop->extent,
                                 loop->for_type, loop->device_api, mutate(loop->body));

            // Wrap lets for the lifted invariants
            for (const auto &p : lifter.lifted) {
                new_stmt = LetStmt::make(p.second, p.first, new_stmt);
            }

            stmt = new_stmt;
        }

        in_gpu_loop = old_in_gpu_loop;
    }
};


// Reassociate summations to group together the loop invariants. Useful to run before LICM.
class GroupLoopInvariants : public IRMutator {
    using IRMutator::visit;

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
        if (is_const(e)) return 0x7fffffff;
        ExprDepth depth(var_depth);
        e.accept(&depth);
        return depth.result;
    }

    struct Term {
        Expr expr;
        bool positive;
        int depth;
    };

    vector<Term> extract_summation(Expr e) {
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

        // Sort the terms by loop depth
        std::sort(terms.begin(), terms.end(),
                  [](const Term &a, const Term &b) {
                      return a.depth > b.depth;
                  });

        return terms;
    }

    Expr reassociate_summation(Expr e) {

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

    void visit(const Sub *op) {
        expr = reassociate_summation(op);
    }

    void visit(const Add *op) {
        expr = reassociate_summation(op);
    }

    int depth = 0;

    void visit(const For *op) {
        depth++;
        var_depth.push(op->name, depth);
        IRMutator::visit(op);
        var_depth.pop(op->name);
        depth--;
    }

    void visit(const Let *op) {
        var_depth.push(op->name, expr_depth(op->value));
        IRMutator::visit(op);
        var_depth.pop(op->name);
    }

    void visit(const LetStmt *op) {
        var_depth.push(op->name, expr_depth(op->value));
        IRMutator::visit(op);
        var_depth.pop(op->name);
    }
};

// Turn for loops of size one into let statements
Stmt loop_invariant_code_motion(Stmt s) {
    s = GroupLoopInvariants().mutate(s);
    s = common_subexpression_elimination(s);
    s = LICM().mutate(s);
    return s;
}

}
}

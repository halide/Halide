#include "LICM.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;

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

// Turn for loops of size one into let statements
Stmt loop_invariant_code_motion(Stmt s) {
    return LICM().mutate(s);
}

}
}

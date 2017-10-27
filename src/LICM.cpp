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
            auto it = lifted.find(e);
            if (it == lifted.end()) {
                string name = unique_name('t');
                lifted[e] = name;
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

    bool in_gpu_loop {false};

    Stmt visit(const For *op) override {
        Stmt stmt;

        bool old_in_gpu_loop = in_gpu_loop;
        in_gpu_loop =
            (op->for_type == ForType::GPUBlock ||
             op->for_type == ForType::GPUThread);

        if (old_in_gpu_loop && in_gpu_loop) {
            // Don't lift lets to in-between gpu blocks/threads
            stmt = IRMutator2::visit(op);
        } else if (op->device_api == DeviceAPI::GLSL ||
                   op->device_api == DeviceAPI::OpenGLCompute) {
            // Don't lift anything out of OpenGL loops
            stmt = IRMutator2::visit(op);
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
        return stmt;
    }
};

// Turn for loops of size one into let statements
Stmt loop_invariant_code_motion(Stmt s) {
    return LICM().mutate(s);
}

}
}

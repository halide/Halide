#include "UpcastBufferIndices.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"

using std::set;
using std::string;

namespace Halide {
namespace Internal {

class UpcastBufferIndices : public IRMutator {
    class UpcastVariables : public IRMutator {
        Scope<Expr> scope;
        using IRMutator::visit;

        Type get_type(Expr e) const {
            if (const Variable *v = e.as<Variable>()) {
                if (!scope.contains(v->name)) {
                    // If a variable is not in scope, it is an input
                    // or output buffer min/extent. For now, buffer
                    // mins/extents are still 32 bits.
                    return Int(32);
                }
                return get_type(scope.get(v->name));
            }
            return e.type();
        }

        Expr upcast(Expr e) const {
            Type t = Int(64, e.type().width);
            if (e.type() == t) {
                return e;
            } else {
                return Cast::make(t, e);
            }
        }

        void visit(const Variable *op) {
            if (starts_with(op->name, prefix)) {
                expr = upcast(op);
            } else {
                expr = op;
            }
        }

        template<typename T>
        void mutate_binary_operator(const T *op) {
            Expr a = mutate(op->a);
            Expr b = mutate(op->b);
            bool modified = !a.same_as(op->a) || !b.same_as(op->b);
            bool type_mismatch = get_type(a) != get_type(b);
            if (modified || type_mismatch) {
                expr = T::make(upcast(a), upcast(b));
            } else {
                expr = op;
            }
        }

        void visit(const Add *op) {mutate_binary_operator(op);}
        void visit(const Sub *op) {mutate_binary_operator(op);}
        void visit(const Mul *op) {mutate_binary_operator(op);}
        void visit(const Div *op) {mutate_binary_operator(op);}
        void visit(const Mod *op) {mutate_binary_operator(op);}
        void visit(const Min *op) {mutate_binary_operator(op);}
        void visit(const Max *op) {mutate_binary_operator(op);}

        void visit(const Let *op) {
            Expr value = mutate(op->value);
            scope.push(op->name, value);
            Expr body = mutate(op->body);
            scope.pop(op->name);
            if (!value.same_as(op->value) || !body.same_as(op->body)) {
                expr = Let::make(op->name, value, body);
            } else {
                expr = op;
            }
        }

        void visit(const LetStmt *op) {
            Expr value = mutate(op->value);
            scope.push(op->name, value);
            Stmt body = mutate(op->body);
            scope.pop(op->name);
            if (!value.same_as(op->value) || !body.same_as(op->body)) {
                stmt = LetStmt::make(op->name, value, body);
            } else {
                stmt = op;
            }
        }

        void visit(const Ramp *op) {
            Expr base = mutate(op->base);
            Expr stride = mutate(op->stride);
            bool modified = !base.same_as(op->base) || !stride.same_as(op->stride);
            if (modified) {
                expr = Ramp::make(upcast(base), upcast(stride), op->width);
            } else {
                expr = op;
            }
        }
    public:
        const string &prefix;
        UpcastVariables(const string &p) : prefix(p) {}
    };

    using IRMutator::visit;

    void visit(const Load *op) {
        UpcastVariables vars(op->name);
        Expr index = vars.mutate(op->index);
        expr = Load::make(op->type, op->name, index, op->image, op->param);
    }

    void visit(const Store *op) {
        UpcastVariables vars(op->name);
        Expr index = vars.mutate(op->index);
        stmt = Store::make(op->name, op->value, index);
    }
};

Stmt upcast_buffer_indices(Stmt s) {
    return UpcastBufferIndices().mutate(s);
}

}
}

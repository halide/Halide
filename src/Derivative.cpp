#include "Derivative.h"
#include "IRMutator.h"
#include "Substitute.h"
#include "Scope.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::string;

class FiniteDifference : public IRMutator {
    Scope<Expr> scope;
    string var;

    Expr brute_force(Expr e) {
        return substitute(var, (Variable::make(Int(32), var)) + 1, e) - e;
    }

    using IRMutator::visit;

    void visit(const IntImm *) {
        expr = 0;
    }

    void visit(const FloatImm *) {
        expr = 0.0f;
    }

    void visit(const Cast *op) {
        expr = brute_force(op);
    }

    void visit(const Variable *op) {
        if (op->name == var) {
            expr = make_one(op->type);
        } else if (scope.contains(op->name)) {
            expr = scope.get(op->name);
        } else {
            expr = make_zero(op->type);
        }
    }

    void visit(const Add *op) {
        expr = mutate(op->a) + mutate(op->b);
    }

    void visit(const Sub *op) {
        expr = mutate(op->a) - mutate(op->b);
    }

    void visit(const Mul *op) {
        Expr da = mutate(op->a), db = mutate(op->b);
        expr = op->a * db + da * op->b + da * db;
    }

    void visit(const Div *op) {
        expr = brute_force(op);
    }

    void visit(const Mod *op) {
        expr = brute_force(op);
    }

    void visit(const Min *op) {
        expr = select(op->a < op->b, mutate(op->a), mutate(op->b));
    }

    void visit(const Max *op) {
        expr = select(op->a > op->b, mutate(op->a), mutate(op->b));
    }

    void visit(const Select *op) {
        expr = select(op->condition, mutate(op->true_value), mutate(op->false_value));
    }

    void visit(const Load *op) {
        expr = brute_force(op);
    }

    void visit(const Call *op) {
        expr = brute_force(op);
    }

    void visit(const Let *op) {
        scope.push(op->name, mutate(op->value));
        expr = mutate(op->body);
        scope.pop(op->name);
    }
public:
    FiniteDifference(string v) : var(v) {}
};

Expr finite_difference(Expr expr, const string &var) {
    return FiniteDifference(var).mutate(expr);
}

class Monotonic : public IRVisitor {
    const string &var;

    Scope<MonotonicResult> scope;

    void visit(const IntImm *) {
        result = Constant;
    }

    void visit(const FloatImm *) {
        result = Constant;
    }

    void visit(const StringImm *) {
        internal_error << "Monotonic on String\n";
    }

    void visit(const Cast *op) {
        op->value.accept(this);

        if (op->type.can_represent(op->value.type())) {
            // No overflow.
            return;
        }

        if (op->value.type().bits >= 32 && op->type.bits >= 32) {
            // We assume 32-bit types don't overflow.
            return;
        }

        // A narrowing cast. There may be more cases we can catch, but
        // for now we punt.
        if (result != Constant) {
            result = Unknown;
        }
    }

    void visit(const Variable *op) {
        if (op->name == var) {
            result = MonotonicIncreasing;
        } else if (scope.contains(op->name)) {
            result = scope.get(op->name);
        } else {
            result = Constant;
        }
    }

    MonotonicResult flip(MonotonicResult r) {
        switch (r) {
        case MonotonicIncreasing: return MonotonicDecreasing;
        case MonotonicDecreasing: return MonotonicIncreasing;
        default: return r;
        }
    }

    MonotonicResult unify(MonotonicResult a, MonotonicResult b) {
        if (a == b) {
            return a;
        }

        if (a == Unknown || b == Unknown) {
            return Unknown;
        }

        if (a == Constant) {
            return b;
        }

        if (b == Constant) {
            return a;
        }

        return Unknown;
    }

    void visit(const Add *op) {
        op->a.accept(this);
        MonotonicResult ra = result;
        op->b.accept(this);
        MonotonicResult rb = result;
        result = unify(ra, rb);
    }

    void visit(const Sub *op) {
        op->a.accept(this);
        MonotonicResult ra = result;
        op->b.accept(this);
        MonotonicResult rb = result;
        result = unify(ra, flip(rb));
    }

    void visit(const Mul *op) {
        op->a.accept(this);
        MonotonicResult ra = result;
        op->b.accept(this);
        MonotonicResult rb = result;
        result = unify(ra, rb);
    }

    void visit(const Div *op) {
        op->a.accept(this);
        MonotonicResult ra = result;
        op->b.accept(this);
        MonotonicResult rb = result;
        result = unify(ra, flip(rb));
    }

    void visit(const Mod *op) {
        result = Unknown;
    }

    void visit(const Min *op) {
        op->a.accept(this);
        MonotonicResult ra = result;
        op->b.accept(this);
        MonotonicResult rb = result;
        result = unify(ra, rb);
    }

    void visit(const Max *op) {
        op->a.accept(this);
        MonotonicResult ra = result;
        op->b.accept(this);
        MonotonicResult rb = result;
        result = unify(ra, rb);
    }

    void visit_cmp(Expr a, Expr b) {
        a.accept(this);
        MonotonicResult ra = result;
        b.accept(this);
        MonotonicResult rb = result;
        if (ra == Constant && rb == Constant) {
            result = Constant;
        } else {
            result = Unknown;
        }
    }

    void visit(const EQ *op) {
        visit_cmp(op->a, op->b);
    }

    void visit(const NE *op) {
        visit_cmp(op->a, op->b);
    }

    void visit(const LT *op) {
        visit_cmp(op->a, op->b);
    }

    void visit(const LE *op) {
        visit_cmp(op->a, op->b);
    }

    void visit(const GT *op) {
        visit_cmp(op->a, op->b);
    }

    void visit(const GE *op) {
        visit_cmp(op->a, op->b);
    }

    void visit(const And *op) {
        visit_cmp(op->a, op->b);
    }

    void visit(const Or *op) {
        visit_cmp(op->a, op->b);
    }

    void visit(const Not *op) {
        op->a.accept(this);
    }

    void visit(const Select *op) {
        op->condition.accept(this);
        MonotonicResult rcond = result;

        if (rcond != Constant) {
            result = Unknown;
        } else {
            op->true_value.accept(this);
            MonotonicResult ra = result;
            op->false_value.accept(this);
            MonotonicResult rb = result;
            result = unify(ra, rb);
        }
    }

    void visit(const Load *op) {
        op->index.accept(this);
        if (result != Constant) {
            result = Unknown;
        }
    }

    void visit(const Ramp *op) {
        internal_error << "Monotonic of vector\n";
    }

    void visit(const Broadcast *op) {
        internal_error << "Monotonic of vector\n";
    }

    void visit(const Call *op) {
        for (size_t i = 0; i < op->args.size(); i++) {
            op->args[i].accept(this);
            if (result != Constant) {
                result = Unknown;
                return;
            }
        }
        result = Constant;
    }

    void visit(const Let *op) {
        op->value.accept(this);

        if (result == Constant) {
            // No point pushing it if it's constant w.r.t the var,
            // because unknown variables are treated as constant.
            op->body.accept(this);
        } else {
            scope.push(op->name, result);
            op->body.accept(this);
            scope.pop(op->name);
        }
    }

    void visit(const LetStmt *op) {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const AssertStmt *op) {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Pipeline *op) {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const For *op) {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Store *op) {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Provide *op) {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Allocate *op) {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Free *op) {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Realize *op) {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Block *op) {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const IfThenElse *op) {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Evaluate *op) {
        internal_error << "Monotonic of statement\n";
    }

public:
    MonotonicResult result;

    Monotonic(const std::string &v) : var(v), result(Unknown) {}
};

MonotonicResult is_monotonic(Expr e, const std::string &var) {
    if (!e.defined()) return Unknown;
    Monotonic m(var);
    e.accept(&m);
    return m.result;
}

}
}

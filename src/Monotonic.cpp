#include "Monotonic.h"
#include "IRMutator.h"
#include "Substitute.h"
#include "Scope.h"
#include "Simplify.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::string;

class MonotonicVisitor : public IRVisitor {
    const string &var;

    Scope<Monotonic> scope;

    void visit(const IntImm *) {
        result = Monotonic::Constant;
    }

    void visit(const UIntImm *) {
        result = Monotonic::Constant;
    }

    void visit(const FloatImm *) {
        result = Monotonic::Constant;
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

        if (op->value.type().bits() >= 32 && op->type.bits() >= 32) {
            // We assume 32-bit types don't overflow.
            return;
        }

        // A narrowing cast. There may be more cases we can catch, but
        // for now we punt.
        if (result != Monotonic::Constant) {
            result = Monotonic::Unknown;
        }
    }

    void visit(const Variable *op) {
        if (op->name == var) {
            result = Monotonic::Increasing;
        } else if (scope.contains(op->name)) {
            result = scope.get(op->name);
        } else {
            result = Monotonic::Constant;
        }
    }

    Monotonic flip(Monotonic r) {
        switch (r) {
        case Monotonic::Increasing: return Monotonic::Decreasing;
        case Monotonic::Decreasing: return Monotonic::Increasing;
        default: return r;
        }
    }

    Monotonic unify(Monotonic a, Monotonic b) {
        if (a == b) {
            return a;
        }

        if (a == Monotonic::Unknown || b == Monotonic::Unknown) {
            return Monotonic::Unknown;
        }

        if (a == Monotonic::Constant) {
            return b;
        }

        if (b == Monotonic::Constant) {
            return a;
        }

        return Monotonic::Unknown;
    }

    void visit(const Add *op) {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;
        result = unify(ra, rb);
    }

    void visit(const Sub *op) {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;
        result = unify(ra, flip(rb));
    }

    void visit(const Mul *op) {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;

        if (ra == Monotonic::Constant && rb == Monotonic::Constant) {
            result = Monotonic::Constant;
        } else if (is_positive_const(op->a)) {
            result = rb;
        } else if (is_positive_const(op->b)) {
            result = ra;
        } else if (is_negative_const(op->a)) {
            result = flip(rb);
        } else if (is_negative_const(op->b)) {
            result = flip(ra);
        } else {
            result = Monotonic::Unknown;
        }

    }

    void visit(const Div *op) {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;

        if (ra == Monotonic::Constant && rb == Monotonic::Constant) {
            result = Monotonic::Constant;
        } else if (is_positive_const(op->b)) {
            result = ra;
        } else if (is_negative_const(op->b)) {
            result = flip(ra);
        } else {
            result = Monotonic::Unknown;
        }
    }

    void visit(const Mod *op) {
        result = Monotonic::Unknown;
    }

    void visit(const Min *op) {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;
        result = unify(ra, rb);
    }

    void visit(const Max *op) {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;
        result = unify(ra, rb);
    }

    void visit_eq(Expr a, Expr b) {
        a.accept(this);
        Monotonic ra = result;
        b.accept(this);
        Monotonic rb = result;
        if (ra == Monotonic::Constant && rb == Monotonic::Constant) {
            result = Monotonic::Constant;
        } else {
            result = Monotonic::Unknown;
        }
    }

    void visit(const EQ *op) {
        visit_eq(op->a, op->b);
    }

    void visit(const NE *op) {
        visit_eq(op->a, op->b);
    }

    void visit_lt(Expr a, Expr b) {
        a.accept(this);
        Monotonic ra = result;
        b.accept(this);
        Monotonic rb = result;
        result = unify(flip(ra), rb);
    }

    void visit(const LT *op) {
        visit_lt(op->a, op->b);
    }

    void visit(const LE *op) {
        visit_lt(op->a, op->b);
    }

    void visit(const GT *op) {
        visit_lt(op->b, op->a);
    }

    void visit(const GE *op) {
        visit_lt(op->b, op->a);
    }

    void visit(const And *op) {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;
        result = unify(ra, rb);
    }

    void visit(const Or *op) {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;
        result = unify(ra, rb);
    }

    void visit(const Not *op) {
        op->a.accept(this);
        result = flip(result);
    }

    void visit(const Select *op) {
        op->condition.accept(this);
        Monotonic rcond = result;

        op->true_value.accept(this);
        Monotonic ra = result;
        op->false_value.accept(this);
        Monotonic rb = result;
        Monotonic unified = unify(ra, rb);

        if (rcond == Monotonic::Constant) {
            result = unified;
            return;
        }

        bool true_value_ge_false_value = can_prove(op->true_value >= op->false_value);
        bool true_value_le_false_value = can_prove(op->true_value <= op->false_value);

        bool switches_from_true_to_false = rcond == Monotonic::Decreasing;
        bool switches_from_false_to_true = rcond == Monotonic::Increasing;

        if (true_value_ge_false_value &&
            true_value_le_false_value) {
            // The true value equals the false value.
            result = ra;
        } else if ((unified == Monotonic::Increasing || unified == Monotonic::Constant) &&
            ((switches_from_false_to_true && true_value_ge_false_value) ||
             (switches_from_true_to_false && true_value_le_false_value))) {
            // Both paths increase, and the condition makes it switch
            // from the lesser path to the greater path.
            result = Monotonic::Increasing;
        } else if ((unified == Monotonic::Decreasing || unified == Monotonic::Constant) &&
                   ((switches_from_false_to_true && true_value_le_false_value) ||
                    (switches_from_true_to_false && true_value_ge_false_value))) {
            // Both paths decrease, and the condition makes it switch
            // from the greater path to the lesser path.
            result = Monotonic::Decreasing;
        } else {
            result = Monotonic::Unknown;
        }
    }

    void visit(const Load *op) {
        op->index.accept(this);
        if (result != Monotonic::Constant) {
            result = Monotonic::Unknown;
        }
    }

    void visit(const Ramp *op) {
        internal_error << "Monotonic of vector\n";
    }

    void visit(const Broadcast *op) {
        internal_error << "Monotonic of vector\n";
    }

    void visit(const Call *op) {
        // Some functions are known to be monotonic
        if (op->is_intrinsic(Call::likely) ||
            op->is_intrinsic(Call::likely_if_innermost) ||
            op->is_intrinsic(Call::return_second)) {
            op->args.back().accept(this);
            return;
        }

        for (size_t i = 0; i < op->args.size(); i++) {
            op->args[i].accept(this);
            if (result != Monotonic::Constant) {
                result = Monotonic::Unknown;
                return;
            }
        }
        result = Monotonic::Constant;
    }

    void visit(const Let *op) {
        op->value.accept(this);

        if (result == Monotonic::Constant) {
            // No point pushing it if it's constant w.r.t the var,
            // because unknown variables are treated as constant.
            op->body.accept(this);
        } else {
            scope.push(op->name, result);
            op->body.accept(this);
            scope.pop(op->name);
        }
    }

    void visit(const Shuffle *op) {
        for (size_t i = 0; i < op->vectors.size(); i++) {
            op->vectors[i].accept(this);
            if (result != Monotonic::Constant) {
                result = Monotonic::Unknown;
                return;
            }
        }
        result = Monotonic::Constant;
    }

    void visit(const LetStmt *op) {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const AssertStmt *op) {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const ProducerConsumer *op) {
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

    void visit(const Prefetch *op) {
        internal_error << "Monotonic of statement\n";
    }

public:
    Monotonic result;

    MonotonicVisitor(const std::string &v) : var(v), result(Monotonic::Unknown) {}
};

Monotonic is_monotonic(Expr e, const std::string &var) {
    if (!e.defined()) return Monotonic::Unknown;
    MonotonicVisitor m(var);
    e.accept(&m);
    return m.result;
}

namespace {
void check_increasing(Expr e) {
    internal_assert(is_monotonic(e, "x") == Monotonic::Increasing)
        << "Was supposed to be increasing: " << e << "\n";
}

void check_decreasing(Expr e) {
    internal_assert(is_monotonic(e, "x") == Monotonic::Decreasing)
        << "Was supposed to be decreasing: " << e << "\n";
}

void check_constant(Expr e) {
    internal_assert(is_monotonic(e, "x") == Monotonic::Constant)
        << "Was supposed to be constant: " << e << "\n";
}

void check_unknown(Expr e) {
    internal_assert(is_monotonic(e, "x") == Monotonic::Unknown)
        << "Was supposed to be unknown: " << e << "\n";
}
}

void is_monotonic_test() {

    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");

    check_increasing(x);
    check_increasing(x+4);
    check_increasing(x+y);
    check_increasing(x*4);
    check_increasing(min(x+4, y+4));
    check_increasing(max(x+y, x-y));
    check_increasing(x >= y);
    check_increasing(x > y);

    check_decreasing(-x);
    check_decreasing(x*-4);
    check_decreasing(y - x);
    check_decreasing(x < y);
    check_decreasing(x <= y);

    check_unknown(x == y);
    check_unknown(x != y);
    check_unknown(x*y);

    check_increasing(select(y == 2, x, x+4));
    check_decreasing(select(y == 2, -x, x*-4));

    check_increasing(select(x > 2, x+1, x));
    check_increasing(select(x < 2, x, x+1));
    check_decreasing(select(x > 2, -x-1, -x));
    check_decreasing(select(x < 2, -x, -x-1));

    check_unknown(select(x < 2, x, x-5));
    check_unknown(select(x > 2, x-5, x));

    check_constant(y);

    check_increasing(select(x < 17, y, y+1));
    check_increasing(select(x > 17, y, y-1));
    check_decreasing(select(x < 17, y, y-1));
    check_decreasing(select(x > 17, y, y+1));

    check_increasing(select(x % 2 == 0, x+3, x+3));

    check_constant(select(y > 3, y + 23, y - 65));

    std::cout << "is_monotonic test passed" << std::endl;
}


}
}

#include "Monotonic.h"
#include "IRMutator.h"
#include "Substitute.h"
#include "Scope.h"
#include "Simplify.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::string;

class Monotonic : public IRVisitor {
    const string &var;

    Scope<MonotonicResult> scope;

    void visit(const IntImm *) {
        result = Constant;
    }

    void visit(const UIntImm *) {
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

        if (op->value.type().bits() >= 32 && op->type.bits() >= 32) {
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

        if (ra == Constant && rb == Constant) {
            result = Constant;
        } else if (is_positive_const(op->a)) {
            result = rb;
        } else if (is_positive_const(op->b)) {
            result = ra;
        } else if (is_negative_const(op->a)) {
            result = flip(rb);
        } else if (is_negative_const(op->b)) {
            result = flip(ra);
        } else {
            result = Unknown;
        }

    }

    void visit(const Div *op) {
        op->a.accept(this);
        MonotonicResult ra = result;
        op->b.accept(this);
        MonotonicResult rb = result;

        if (ra == Constant && rb == Constant) {
            result = Constant;
        } else if (is_positive_const(op->b)) {
            result = ra;
        } else if (is_negative_const(op->b)) {
            result = flip(ra);
        } else {
            result = Unknown;
        }
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

    void visit_eq(Expr a, Expr b) {
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
        visit_eq(op->a, op->b);
    }

    void visit(const NE *op) {
        visit_eq(op->a, op->b);
    }

    void visit_lt(Expr a, Expr b) {
        a.accept(this);
        MonotonicResult ra = result;
        b.accept(this);
        MonotonicResult rb = result;
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
        MonotonicResult ra = result;
        op->b.accept(this);
        MonotonicResult rb = result;
        result = unify(ra, rb);
    }

    void visit(const Or *op) {
        op->a.accept(this);
        MonotonicResult ra = result;
        op->b.accept(this);
        MonotonicResult rb = result;
        result = unify(ra, rb);
    }

    void visit(const Not *op) {
        op->a.accept(this);
        result = flip(result);
    }

    void visit(const Select *op) {
        op->condition.accept(this);
        MonotonicResult rcond = result;

        op->true_value.accept(this);
        MonotonicResult ra = result;
        op->false_value.accept(this);
        MonotonicResult rb = result;
        MonotonicResult unified = unify(ra, rb);

        if (rcond == Constant) {
            result = unified;
            return;
        }

        bool true_value_ge_false_value = is_one(simplify(op->true_value >= op->false_value));
        bool true_value_le_false_value = is_one(simplify(op->true_value <= op->false_value));

        bool switches_from_true_to_false = rcond == MonotonicDecreasing;
        bool switches_from_false_to_true = rcond == MonotonicIncreasing;

        if (rcond == Constant) {
            result = unify(ra, rb);
        } else if ((unified == MonotonicIncreasing || unified == Constant) &&
                   ((switches_from_false_to_true && true_value_ge_false_value) ||
                    (switches_from_true_to_false && true_value_le_false_value))) {
            // Both paths increase, and the condition makes it switch
            // from the lesser path to the greater path.
            result = MonotonicIncreasing;
        } else if ((unified == MonotonicDecreasing || unified == Constant) &&
                   ((switches_from_false_to_true && true_value_le_false_value) ||
                    (switches_from_true_to_false && true_value_ge_false_value))) {
            // Both paths decrease, and the condition makes it switch
            // from the greater path to the lesser path.
            result = MonotonicDecreasing;
        } else {
            result = Unknown;
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
        // Some functions are known to be monotonic
        if (op->is_intrinsic(Call::likely)) {
            op->args[0].accept(this);
            return;
        }

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

namespace {
void check_increasing(Expr e) {
    internal_assert(is_monotonic(e, "x") == MonotonicIncreasing)
        << "Was supposed to be increasing: " << e << "\n";
}

void check_decreasing(Expr e) {
    internal_assert(is_monotonic(e, "x") == MonotonicDecreasing)
        << "Was supposed to be decreasing: " << e << "\n";
}

void check_constant(Expr e) {
    internal_assert(is_monotonic(e, "x") == Constant)
        << "Was supposed to be constant: " << e << "\n";
}

void check_unknown(Expr e) {
    internal_assert(is_monotonic(e, "x") == Unknown)
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

    check_constant(select(y > 3, y + 23, y - 65));

    std::cout << "is_monotonic test passed" << std::endl;
}


}
}

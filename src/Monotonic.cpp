#include "Monotonic.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

std::ostream &operator<<(std::ostream &stream, const Monotonic &m) {
    switch (m) {
    case Monotonic::Constant:
        stream << "Constant";
        break;
    case Monotonic::Increasing:
        stream << "Increasing";
        break;
    case Monotonic::Decreasing:
        stream << "Decreasing";
        break;
    case Monotonic::Unknown:
        stream << "Unknown";
        break;
    }
    return stream;
}

using std::string;

class MonotonicVisitor : public IRVisitor {
    const string &var;

    Scope<Monotonic> scope;

    void visit(const IntImm *) override {
        result = Monotonic::Constant;
    }

    void visit(const UIntImm *) override {
        result = Monotonic::Constant;
    }

    void visit(const FloatImm *) override {
        result = Monotonic::Constant;
    }

    void visit(const StringImm *) override {
        // require() Exprs can includes Strings.
        result = Monotonic::Constant;
    }

    void visit(const Cast *op) override {
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

    void visit(const Variable *op) override {
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
        case Monotonic::Increasing:
            return Monotonic::Decreasing;
        case Monotonic::Decreasing:
            return Monotonic::Increasing;
        default:
            return r;
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

    void visit(const Add *op) override {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;
        result = unify(ra, rb);
    }

    void visit(const Sub *op) override {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;
        result = unify(ra, flip(rb));
    }

    void visit(const Mul *op) override {
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

    void visit(const Div *op) override {
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

    void visit(const Mod *op) override {
        result = Monotonic::Unknown;
    }

    void visit(const Min *op) override {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;
        result = unify(ra, rb);
    }

    void visit(const Max *op) override {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;
        result = unify(ra, rb);
    }

    void visit_eq(const Expr &a, const Expr &b) {
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

    void visit(const EQ *op) override {
        visit_eq(op->a, op->b);
    }

    void visit(const NE *op) override {
        visit_eq(op->a, op->b);
    }

    void visit_lt(const Expr &a, const Expr &b) {
        a.accept(this);
        Monotonic ra = result;
        b.accept(this);
        Monotonic rb = result;
        result = unify(flip(ra), rb);
    }

    void visit(const LT *op) override {
        visit_lt(op->a, op->b);
    }

    void visit(const LE *op) override {
        visit_lt(op->a, op->b);
    }

    void visit(const GT *op) override {
        visit_lt(op->b, op->a);
    }

    void visit(const GE *op) override {
        visit_lt(op->b, op->a);
    }

    void visit(const And *op) override {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;
        result = unify(ra, rb);
    }

    void visit(const Or *op) override {
        op->a.accept(this);
        Monotonic ra = result;
        op->b.accept(this);
        Monotonic rb = result;
        result = unify(ra, rb);
    }

    void visit(const Not *op) override {
        op->a.accept(this);
        result = flip(result);
    }

    void visit(const Select *op) override {
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

    void visit(const Load *op) override {
        op->index.accept(this);
        if (result != Monotonic::Constant) {
            result = Monotonic::Unknown;
        }
    }

    void visit(const Ramp *op) override {
        Expr equiv = op->base + Variable::make(op->base.type(), unique_name('t')) * op->stride;
        equiv.accept(this);
    }

    void visit(const Broadcast *op) override {
        op->value.accept(this);
    }

    void visit(const Call *op) override {
        // Some functions are known to be monotonic
        if (op->is_intrinsic(Call::likely) ||
            op->is_intrinsic(Call::likely_if_innermost) ||
            op->is_intrinsic(Call::return_second)) {
            op->args.back().accept(this);
            return;
        }

        if (op->is_intrinsic(Call::unsafe_promise_clamped) ||
            op->is_intrinsic(Call::promise_clamped)) {
            op->args[0].accept(this);
            return;
        }

        if (op->is_intrinsic(Call::require)) {
            // require() returns the value of the second arg in all non-failure cases
            op->args[1].accept(this);
            return;
        }

        if (!op->is_pure()) {
            // Even with constant args, the result could vary from one loop iteration to the next.
            result = Monotonic::Unknown;
            return;
        }

        for (size_t i = 0; i < op->args.size(); i++) {
            op->args[i].accept(this);
            if (result != Monotonic::Constant) {
                // One of the args is not constant.
                result = Monotonic::Unknown;
                return;
            }
        }
        result = Monotonic::Constant;
    }

    void visit(const Let *op) override {
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

    void visit(const Shuffle *op) override {
        for (size_t i = 0; i < op->vectors.size(); i++) {
            op->vectors[i].accept(this);
            if (result != Monotonic::Constant) {
                result = Monotonic::Unknown;
                return;
            }
        }
        result = Monotonic::Constant;
    }

    void visit(const LetStmt *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const AssertStmt *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const ProducerConsumer *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const For *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Acquire *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Store *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Provide *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Allocate *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Free *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Realize *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Block *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Fork *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const IfThenElse *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Evaluate *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Prefetch *op) override {
        internal_error << "Monotonic of statement\n";
    }

    void visit(const Atomic *op) override {
        internal_error << "Monotonic of statement\n";
    }

public:
    Monotonic result;

    MonotonicVisitor(const std::string &v, const Scope<Monotonic> &parent)
        : var(v), result(Monotonic::Unknown) {
        scope.set_containing_scope(&parent);
    }
};

Monotonic is_monotonic(const Expr &e, const std::string &var, const Scope<Monotonic> &scope) {
    if (!e.defined()) return Monotonic::Unknown;
    MonotonicVisitor m(var, scope);
    e.accept(&m);
    return m.result;
}

namespace {
void check_increasing(const Expr &e) {
    internal_assert(is_monotonic(e, "x") == Monotonic::Increasing)
        << "Was supposed to be increasing: " << e << "\n";
}

void check_decreasing(const Expr &e) {
    internal_assert(is_monotonic(e, "x") == Monotonic::Decreasing)
        << "Was supposed to be decreasing: " << e << "\n";
}

void check_constant(const Expr &e) {
    internal_assert(is_monotonic(e, "x") == Monotonic::Constant)
        << "Was supposed to be constant: " << e << "\n";
}

void check_unknown(const Expr &e) {
    internal_assert(is_monotonic(e, "x") == Monotonic::Unknown)
        << "Was supposed to be unknown: " << e << "\n";
}
}  // namespace

void is_monotonic_test() {

    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");

    check_increasing(x);
    check_increasing(x + 4);
    check_increasing(x + y);
    check_increasing(x * 4);
    check_increasing(min(x + 4, y + 4));
    check_increasing(max(x + y, x - y));
    check_increasing(x >= y);
    check_increasing(x > y);

    check_decreasing(-x);
    check_decreasing(x * -4);
    check_decreasing(y - x);
    check_decreasing(x < y);
    check_decreasing(x <= y);

    check_unknown(x == y);
    check_unknown(x != y);
    check_unknown(x * y);

    // Not constant despite having constant args, because there's a side-effect.
    check_unknown(Call::make(Int(32), "foo", {Expr(3)}, Call::Extern));

    check_increasing(select(y == 2, x, x + 4));
    check_decreasing(select(y == 2, -x, x * -4));

    check_increasing(select(x > 2, x + 1, x));
    check_increasing(select(x < 2, x, x + 1));
    check_decreasing(select(x > 2, -x - 1, -x));
    check_decreasing(select(x < 2, -x, -x - 1));

    check_unknown(select(x < 2, x, x - 5));
    check_unknown(select(x > 2, x - 5, x));

    check_constant(y);

    check_increasing(select(x < 17, y, y + 1));
    check_increasing(select(x > 17, y, y - 1));
    check_decreasing(select(x < 17, y, y - 1));
    check_decreasing(select(x > 17, y, y + 1));

    check_increasing(select(x % 2 == 0, x + 3, x + 3));

    check_constant(select(y > 3, y + 23, y - 65));

    std::cout << "is_monotonic test passed" << std::endl;
}

}  // namespace Internal
}  // namespace Halide

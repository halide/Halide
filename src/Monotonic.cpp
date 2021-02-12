#include "Monotonic.h"
#include "IROperator.h"
#include "IRVisitor.h"
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

namespace {

Interval constant_interval = Interval::single_point(make_zero(Int(32)));

bool is_constant(const Interval &a) {
    return a.has_lower_bound() && a.has_upper_bound() && can_prove(a.min == 0 && a.max == 0);
}

bool is_monotonic_increasing(const Interval &a) {
    return a.has_lower_bound() && can_prove(a.min >= 0);
}

bool is_monotonic_decreasing(const Interval &a) {
    return a.has_upper_bound() && can_prove(a.max <= 0);
}

Interval to_interval(Monotonic m) {
    switch (m) {
    case Monotonic::Constant:
        return constant_interval;
    case Monotonic::Increasing:
        return Interval(make_zero(Int(32)), Interval::pos_inf());
    case Monotonic::Decreasing:
        return Interval(Interval::neg_inf(), make_zero(Int(32)));
    case Monotonic::Unknown:
        return Interval();
    }
    return Interval();
}

Monotonic to_monotonic(const Interval &x) {
    if (is_constant(x)) {
        return Monotonic::Constant;
    } else if (is_monotonic_increasing(x)) {
        return Monotonic::Increasing;
    } else if (is_monotonic_decreasing(x)) {
        return Monotonic::Decreasing;
    } else {
        return Monotonic::Unknown;
    }
}

Interval unify(const Interval &a, const Interval &b) {
    return Interval::make_union(a, b);
}

Interval unify(const Interval &a, const Expr &b) {
    Interval result;
    result.include(b);
    return result;
}

// Helpers for doing arithmetic on intervals that avoid generating
// expressions of pos_inf/neg_inf.
Interval add(const Interval &a, const Interval &b) {
    Interval result;
    result.min = a.has_lower_bound() && b.has_lower_bound() ? a.min + b.min : Interval::neg_inf();
    result.max = a.has_upper_bound() && b.has_upper_bound() ? a.max + b.max : Interval::pos_inf();
    return result;
}

Interval add(const Interval &a, const Expr &b) {
    Interval result;
    result.min = a.has_lower_bound() ? a.min + b : Interval::neg_inf();
    result.max = a.has_upper_bound() ? a.max + b : Interval::pos_inf();
    return result;
}

Interval multiply(const Interval &a, const Expr &b) {
    Expr x = a.has_lower_bound() ? a.min * b : a.min;
    Expr y = a.has_upper_bound() ? a.max * b : a.max;
    return Interval(Interval::make_min(x, y), Interval::make_max(x, y));
}

Interval divide(const Interval &a, const Expr &b) {
    Expr x = a.has_lower_bound() ? a.min / b : a.min;
    Expr y = a.has_upper_bound() ? (a.max + (abs(b) - 1)) / b : a.max;
    return Interval(Interval::make_min(x, y), Interval::make_max(x, y));
}

Interval negate(const Interval &r) {
    Expr min = r.has_upper_bound() ? -r.max : Interval::neg_inf();
    Expr max = r.has_lower_bound() ? -r.min : Interval::pos_inf();
    return Interval(min, max);
}

class DerivativeBounds : public IRVisitor {
    const string &var;

    Scope<Interval> scope;

    void visit(const IntImm *) override {
        result = constant_interval;
    }

    void visit(const UIntImm *) override {
        result = constant_interval;
    }

    void visit(const FloatImm *) override {
        result = constant_interval;
    }

    void visit(const StringImm *) override {
        // require() Exprs can includes Strings.
        result = constant_interval;
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
        if (!is_constant(result)) {
            result = Interval();
        }
    }

    void visit(const Variable *op) override {
        if (op->name == var) {
            result = Interval::single_point(make_one(Int(32)));
        } else if (scope.contains(op->name)) {
            result = scope.get(op->name);
        } else {
            result = constant_interval;
        }
    }

    void visit(const Add *op) override {
        op->a.accept(this);
        Interval ra = result;
        op->b.accept(this);
        Interval rb = result;
        result = add(ra, rb);
    }

    void visit(const Sub *op) override {
        op->a.accept(this);
        Interval ra = result;
        op->b.accept(this);
        Interval rb = negate(result);
        result = add(ra, rb);
    }

    void visit(const Mul *op) override {
        if (op->type.is_scalar()) {
            op->a.accept(this);
            Interval ra = result;
            op->b.accept(this);
            Interval rb = result;

            // This is very much like the product rule for derivatives.
            if (is_constant(rb)) {
                // Avoid generating large expressions in the common case of constant b.
                result = multiply(ra, op->b);
            } else {
                result = add(multiply(ra, op->b), multiply(rb, op->a));
            }
        } else {
            result = Interval();
        }
    }

    void visit(const Div *op) override {
        if (op->type.is_scalar()) {
            op->a.accept(this);
            Interval ra = result;
            op->b.accept(this);
            Interval rb = result;

            // This is much like the quotient rule for derivatives.
            if (is_constant(rb)) {
                // Avoid generating large expressions in the common case of constant b.
                result = divide(ra, op->b);
            } else {
                result = divide(add(multiply(ra, op->b), negate(multiply(rb, op->a))), op->b * op->b);
            }
        } else {
            result = Interval();
        }
    }

    void visit(const Mod *op) override {
        result = Interval();
    }

    void visit(const Min *op) override {
        op->a.accept(this);
        Interval ra = result;
        op->b.accept(this);
        Interval rb = result;
        result = unify(ra, rb);
    }

    void visit(const Max *op) override {
        op->a.accept(this);
        Interval ra = result;
        op->b.accept(this);
        Interval rb = result;
        result = unify(ra, rb);
    }

    void visit_eq(const Expr &a, const Expr &b) {
        a.accept(this);
        Interval ra = result;
        b.accept(this);
        Interval rb = result;
        if (is_constant(ra) && is_constant(rb)) {
            result = constant_interval;
        } else {
            result = Interval(make_const(Int(32), -1), make_one(Int(32)));
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
        Interval ra = result;
        b.accept(this);
        Interval rb = result;
        result = unify(negate(ra), rb);
        result.min = Interval::make_max(result.min, make_const(Int(32), -1));
        result.max = Interval::make_min(result.max, make_one(Int(32)));
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
        Interval ra = result;
        op->b.accept(this);
        Interval rb = result;
        result = unify(ra, rb);
    }

    void visit(const Or *op) override {
        op->a.accept(this);
        Interval ra = result;
        op->b.accept(this);
        Interval rb = result;
        result = unify(ra, rb);
    }

    void visit(const Not *op) override {
        op->a.accept(this);
        result = negate(result);
    }

    void visit(const Select *op) override {
        op->condition.accept(this);
        Interval rcond = result;

        op->true_value.accept(this);
        Interval ra = result;
        op->false_value.accept(this);
        Interval rb = result;
        Interval unified = unify(ra, rb);

        // The result is the unified bounds, added to the "bump" that happens when switching from true to false.
        if (op->type.is_scalar()) {
            Expr switch_step = simplify(op->true_value - op->false_value);
            Interval switch_bounds = multiply(rcond, switch_step);
            result = add(unified, switch_bounds);
        } else {
            result = Interval();
        }
    }

    void visit(const Load *op) override {
        op->index.accept(this);
        if (!is_constant(result)) {
            result = Interval();
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

        if (!op->is_pure() || !is_constant(result)) {
            // Even with constant args, the result could vary from one loop iteration to the next.
            result = Interval();
            return;
        }

        for (size_t i = 0; i < op->args.size(); i++) {
            op->args[i].accept(this);
            if (!is_constant(result)) {
                // One of the args is not constant.
                result = Interval();
                return;
            }
        }
        result = constant_interval;
    }

    void visit(const Let *op) override {
        op->value.accept(this);

        if (is_constant(result)) {
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
            if (!is_constant(result)) {
                result = Interval();
                return;
            }
        }
        result = constant_interval;
    }

    void visit(const VectorReduce *op) override {
        op->value.accept(this);
        switch (op->op) {
        case VectorReduce::Add:
            result = multiply(result, op->value.type().lanes() / op->type.lanes());
            break;
        case VectorReduce::Min:
        case VectorReduce::Max:
            // These reductions are monotonic in the arg
            break;
        case VectorReduce::Mul:
        case VectorReduce::And:
        case VectorReduce::Or:
            // These ones are not
            if (!is_constant(result)) {
                result = Interval();
            }
        }
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
    Interval result;

    DerivativeBounds(const std::string &v, const Scope<Interval> &parent)
        : var(v), result(Interval()) {
        scope.set_containing_scope(&parent);
    }
};

}  // namespace

Interval derivative_bounds(const Expr &e, const std::string &var, const Scope<Interval> &scope) {
    if (!e.defined()) {
        return Interval();
    }
    DerivativeBounds m(var, scope);
    e.accept(&m);
    return m.result;
}

Monotonic is_monotonic(const Expr &e, const std::string &var, const Scope<Monotonic> &scope) {
    if (!e.defined()) {
        return Monotonic::Unknown;
    }
    Scope<Interval> intervals_scope;
    for (Scope<Monotonic>::const_iterator i = scope.cbegin(); i != scope.cend(); ++i) {
        intervals_scope.push(i.name(), to_interval(i.value()));
    }
    return to_monotonic(derivative_bounds(e, var, intervals_scope));
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
    check_increasing(x / 4);
    check_increasing(min(x + 4, y + 4));
    check_increasing(max(x + y, x - y));
    check_increasing(x >= y);
    check_increasing(x > y);

    check_decreasing(-x);
    check_decreasing(x * -4);
    check_decreasing(x / -4);
    check_decreasing(y - x);
    check_decreasing(x < y);
    check_decreasing(x <= y);

    check_unknown(x == y);
    check_unknown(x != y);
    check_increasing(y <= x);
    check_increasing(y < x);
    check_decreasing(x <= y);
    check_decreasing(x < y);
    check_unknown(x * y);

    // Not constant despite having constant args, because there's a side-effect.
    check_unknown(Call::make(Int(32), "foo", {Expr(3)}, Call::Extern));

    check_increasing(select(y == 2, x, x + 4));
    check_decreasing(select(y == 2, -x, x * -4));

    check_unknown(select(x > 2, x - 2, x));
    check_unknown(select(x < 2, x, x - 2));
    check_unknown(select(x > 2, -x + 2, -x));
    check_unknown(select(x < 2, -x, -x + 2));
    check_increasing(select(x > 2, x - 1, x));
    check_increasing(select(x < 2, x, x - 1));
    check_decreasing(select(x > 2, -x + 1, -x));
    check_decreasing(select(x < 2, -x, -x + 1));

    check_unknown(select(x < 2, x, x - 5));
    check_unknown(select(x > 2, x - 5, x));

    check_constant(y);

    check_increasing(select(x < 17, y, y + 1));
    check_increasing(select(x > 17, y, y - 1));
    check_decreasing(select(x < 17, y, y - 1));
    check_decreasing(select(x > 17, y, y + 1));

    check_increasing(select(x % 2 == 0, x + 3, x + 3));

    check_constant(select(y > 3, y + 23, y - 65));

    check_decreasing(select(2 <= x, 0, 1));
    check_increasing(select(2 <= x, 0, 1) + x);

    std::cout << "is_monotonic test passed" << std::endl;
}

}  // namespace Internal
}  // namespace Halide

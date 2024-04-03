#include "Monotonic.h"
#include "ConstantBounds.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "IRVisitor.h"
#include "Scope.h"
#include "Simplify.h"
#include "SimplifyCorrelatedDifferences.h"
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

const int64_t *as_const_int_or_uint(const Expr &e) {
    if (const int64_t *i = as_const_int(e)) {
        return i;
    } else if (const uint64_t *u = as_const_uint(e)) {
        if (*u <= (uint64_t)std::numeric_limits<int64_t>::max()) {
            return (const int64_t *)u;
        }
    }
    return nullptr;
}

bool is_constant(const ConstantInterval &x) {
    return x.is_single_point(0);
}

ConstantInterval to_interval(Monotonic m) {
    switch (m) {
    case Monotonic::Constant:
        return ConstantInterval::single_point(0);
    case Monotonic::Increasing:
        return ConstantInterval::bounded_below(0);
    case Monotonic::Decreasing:
        return ConstantInterval::bounded_above(0);
    case Monotonic::Unknown:
        return ConstantInterval::everything();
    }
    return ConstantInterval::everything();
}

Monotonic to_monotonic(const ConstantInterval &x) {
    if (is_constant(x)) {
        return Monotonic::Constant;
    } else if (x >= 0) {
        return Monotonic::Increasing;
    } else if (x <= 0) {
        return Monotonic::Decreasing;
    } else {
        return Monotonic::Unknown;
    }
}

class DerivativeBounds : public IRVisitor {
    const string &var;

    // Bounds on the derivatives and values of variables in scope.
    Scope<ConstantInterval> derivative_bounds, value_bounds;

    void visit(const IntImm *) override {
        result = ConstantInterval::single_point(0);
    }

    void visit(const UIntImm *) override {
        result = ConstantInterval::single_point(0);
    }

    void visit(const FloatImm *) override {
        result = ConstantInterval::single_point(0);
    }

    void visit(const StringImm *) override {
        // require() Exprs can includes Strings.
        result = ConstantInterval::single_point(0);
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
            result = ConstantInterval::everything();
        }
    }

    void visit(const Reinterpret *op) override {
        result = ConstantInterval::everything();
    }

    void visit(const Variable *op) override {
        if (op->name == var) {
            result = ConstantInterval::single_point(1);
        } else if (const auto *r = derivative_bounds.find(op->name)) {
            result = *r;
        } else {
            result = ConstantInterval::single_point(0);
        }
    }

    void visit(const Add *op) override {
        op->a.accept(this);
        ConstantInterval ra = result;
        op->b.accept(this);
        result += ra;
    }

    void visit(const Sub *op) override {
        op->b.accept(this);
        ConstantInterval rb = result;
        op->a.accept(this);
        result -= rb;
    }

    void visit(const Mul *op) override {
        if (op->type.is_scalar()) {
            op->a.accept(this);
            ConstantInterval ra = result;
            op->b.accept(this);
            ConstantInterval rb = result;

            // This is essentially the product rule: a*rb + b*ra
            // but only implemented for the case where a or b is constant.
            if (const int64_t *b = as_const_int_or_uint(op->b)) {
                result = ra * (*b);
            } else if (const int64_t *a = as_const_int_or_uint(op->a)) {
                result = rb * (*a);
            } else {
                result = ConstantInterval::everything();
            }
        } else {
            result = ConstantInterval::everything();
        }
    }

    void visit(const Div *op) override {
        if (op->type.is_scalar()) {
            if (const int64_t *b = as_const_int_or_uint(op->b)) {
                op->a.accept(this);
                // We don't just want to divide by b. For the min we want to
                // take floor division, and for the max we want to use ceil
                // division.
                if (*b == 0) {
                    result = ConstantInterval(0, 0);
                } else {
                    if (result.min_defined) {
                        result.min = div_imp(result.min, *b);
                    }
                    if (result.max_defined) {
                        if (result.max != INT64_MIN) {
                            result.max = div_imp(result.max - 1, *b) + 1;
                        } else {
                            result.max_defined = false;
                            result.max = 0;
                        }
                    }
                    if (*b < 0) {
                        result = -result;
                    }
                }
                return;
            }
        }
        result = ConstantInterval::everything();
    }

    void visit(const Mod *op) override {
        // TODO: It's possible to get tighter bounds here. What if neither arg uses the var!
        result = ConstantInterval::everything();
    }

    void visit(const Min *op) override {
        op->a.accept(this);
        ConstantInterval ra = result;
        op->b.accept(this);
        result.include(ra);
    }

    void visit(const Max *op) override {
        op->a.accept(this);
        ConstantInterval ra = result;
        op->b.accept(this);
        result.include(ra);
    }

    void visit_eq(const Expr &a, const Expr &b) {
        a.accept(this);
        ConstantInterval ra = result;
        b.accept(this);
        ConstantInterval rb = result;
        if (is_constant(ra) && is_constant(rb)) {
            result = ConstantInterval::single_point(0);
        } else {
            // If the result is bounded, limit it to [-1, 1]. The largest
            // difference possible is flipping from true to false or false
            // to true.
            result = ConstantInterval(-1, 1);
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
        ConstantInterval ra = result;
        b.accept(this);
        result.include(-ra);
        // If the result is bounded, limit it to [-1, 1]. The largest
        // difference possible is flipping from true to false or false
        // to true.
        result.min = std::min<int64_t>(std::max<int64_t>(result.min, -1), 1);
        result.max = std::min<int64_t>(std::max<int64_t>(result.max, -1), 1);
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
        ConstantInterval ra = result;
        op->b.accept(this);
        result.include(ra);
    }

    void visit(const Or *op) override {
        op->a.accept(this);
        ConstantInterval ra = result;
        op->b.accept(this);
        result.include(ra);
    }

    void visit(const Not *op) override {
        op->a.accept(this);
        result = -result;
    }

    void visit(const Select *op) override {
        // The result is the unified bounds, added to the "bump" that happens
        // when switching from true to false.
        if (op->type.is_scalar()) {
            op->condition.accept(this);
            ConstantInterval rcond = result;
            // rcond is:
            //  [ 0  0] if the condition does not depend on the variable
            //  [-1, 0] if it changes once from true to false
            //  [ 0  1] if it changes once from false to true
            //  [-1, 1] if it could change in either direction

            op->true_value.accept(this);
            ConstantInterval ra = result;
            op->false_value.accept(this);
            result.include(ra);

            // If the condition is not constant, we hit a "bump" when the condition changes value.
            if (!is_constant(rcond)) {
                // It's very important to have stripped likelies here, or the
                // simplification might not cancel things that it should.
                Expr bump = simplify(op->true_value - op->false_value);

                // This is of dubious value, because
                // bound_correlated_differences really assumes you've solved for
                // a variable that you're trying to cancel first. TODO: try
                // removing this.
                bump = bound_correlated_differences(bump);
                ConstantInterval bump_bounds = constant_integer_bounds(bump, value_bounds);
                result += rcond * bump_bounds;
            }
        } else {
            result = ConstantInterval::everything();
        }
    }

    void visit(const Load *op) override {
        op->index.accept(this);
        if (!is_constant(result)) {
            result = ConstantInterval::everything();
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
        if (Call::as_tag(op) ||
            op->is_intrinsic(Call::return_second)) {
            op->args.back().accept(this);
            return;
        }

        if (op->is_intrinsic(Call::saturating_cast)) {
            op->args[0].accept(this);
            result.include(0);
            return;
        }

        if (op->is_intrinsic(Call::require)) {
            // require() returns the value of the second arg in all non-failure cases
            op->args[1].accept(this);
            return;
        }

        if (!op->is_pure() || !is_constant(result)) {
            // Even with constant args, the result could vary from one loop iteration to the next.
            result = ConstantInterval::everything();
            return;
        }

        for (const auto &arg : op->args) {
            arg.accept(this);
            if (!is_constant(result)) {
                // One of the args is not constant.
                result = ConstantInterval::everything();
                return;
            }
        }
        result = ConstantInterval::single_point(0);
    }

    void visit(const Let *op) override {
        op->value.accept(this);

        // As above, this is of dubious value. TODO: Try removing it.
        Expr v = bound_correlated_differences(op->value);
        ScopedBinding<ConstantInterval> vb_binding(value_bounds, op->name,
                                                   constant_integer_bounds(v, value_bounds));
        if (is_constant(result)) {
            // No point pushing it if it's constant w.r.t the var,
            // because unknown variables are treated as constant.
            op->body.accept(this);
        } else {
            ScopedBinding<ConstantInterval> db_binding(derivative_bounds, op->name, result);
            op->body.accept(this);
        }
    }

    void visit(const Shuffle *op) override {
        for (const auto &vector : op->vectors) {
            vector.accept(this);
            if (!is_constant(result)) {
                result = ConstantInterval::everything();
                return;
            }
        }
        result = ConstantInterval::single_point(0);
    }

    void visit(const VectorReduce *op) override {
        op->value.accept(this);
        switch (op->op) {
        case VectorReduce::Add:
        case VectorReduce::SaturatingAdd:
            result *= op->value.type().lanes() / op->type.lanes();
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
                result = ConstantInterval::everything();
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

    void visit(const HoistedStorage *op) override {
        internal_error << "Monotonic of statement\n";
    }

public:
    ConstantInterval result;

    DerivativeBounds(const std::string &v, const Scope<ConstantInterval> &parent)
        : var(v), result(ConstantInterval::everything()) {
        derivative_bounds.set_containing_scope(&parent);
    }
};

}  // namespace

ConstantInterval derivative_bounds(const Expr &e, const std::string &var, const Scope<ConstantInterval> &scope) {
    if (!e.defined()) {
        return ConstantInterval::everything();
    }
    DerivativeBounds m(var, scope);
    remove_likelies(remove_promises(e)).accept(&m);
    return m.result;
}

Monotonic is_monotonic(const Expr &e, const std::string &var, const Scope<ConstantInterval> &scope) {
    if (!e.defined()) {
        return Monotonic::Unknown;
    }
    return to_monotonic(derivative_bounds(e, var, scope));
}

Monotonic is_monotonic(const Expr &e, const std::string &var, const Scope<Monotonic> &scope) {
    if (!e.defined()) {
        return Monotonic::Unknown;
    }
    Scope<ConstantInterval> intervals_scope;
    for (Scope<Monotonic>::const_iterator i = scope.cbegin(); i != scope.cend(); ++i) {
        intervals_scope.push(i.name(), to_interval(i.value()));
    }
    return is_monotonic(e, var, intervals_scope);
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
    Expr z = Variable::make(Int(32), "z");

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

    check_unknown(select(x > 0, y, z));

    check_increasing(select(0 < x, promise_clamped(x - 1, x - 1, z) + 1, promise_clamped(x, x, z)));

    check_constant(y);

    check_increasing(select(x < 17, y, y + 1));
    check_increasing(select(x > 17, y, y - 1));
    check_decreasing(select(x < 17, y, y - 1));
    check_decreasing(select(x > 17, y, y + 1));

    check_increasing(select(x % 2 == 0, x + 3, x + 3));

    check_constant(select(y > 3, y + 23, y - 65));

    check_decreasing(select(2 <= x, 0, 1));
    check_increasing(select(2 <= x, 0, 1) + x);
    check_decreasing(-min(x, 16));

    check_unknown(select(0 < x, max(min(x, 4), 3), 4));

    std::cout << "is_monotonic test passed\n";
}

}  // namespace Internal
}  // namespace Halide

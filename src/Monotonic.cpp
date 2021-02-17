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

bool is_constant(const ConstantInterval &a) {
    return a.is_single_point(0);
}

bool is_monotonic_increasing(const ConstantInterval &a) {
    return a.has_lower_bound() && a.min >= 0;
}

bool is_monotonic_decreasing(const ConstantInterval &a) {
    return a.has_upper_bound() && a.max <= 0;
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
        return ConstantInterval();
    }
    return ConstantInterval();
}

Monotonic to_monotonic(const ConstantInterval &x) {
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

ConstantInterval unify(const ConstantInterval &a, const ConstantInterval &b) {
    return ConstantInterval::make_union(a, b);
}

ConstantInterval unify(const ConstantInterval &a, int64_t b) {
    ConstantInterval result;
    result.include(b);
    return result;
}

// Helpers for doing arithmetic on ConstantIntervals that avoid generating
// expressions of pos_inf/neg_inf.
ConstantInterval add(const ConstantInterval &a, const ConstantInterval &b) {
    ConstantInterval result;
    result.min_defined = a.has_lower_bound() && b.has_lower_bound();
    result.max_defined = a.has_upper_bound() && b.has_upper_bound();
    if (result.has_lower_bound()) {
        result.min = a.min + b.min;
    }
    if (result.has_upper_bound()) {
        result.max = a.max + b.max;
    }
    return result;
}

ConstantInterval add(const ConstantInterval &a, int64_t b) {
    return add(a, ConstantInterval(b, b));
}

ConstantInterval negate(const ConstantInterval &r) {
    ConstantInterval result;
    result.min_defined = r.has_upper_bound();
    result.min = r.has_upper_bound() ? -r.max : 0;
    result.max_defined = r.has_lower_bound();
    result.max = r.has_lower_bound() ? -r.min : 0;
    return result;
}

ConstantInterval sub(const ConstantInterval &a, const ConstantInterval &b) {
    return add(a, negate(b));
}

ConstantInterval sub(const ConstantInterval &a, int64_t b) {
    return sub(a, ConstantInterval(b, b));
}

ConstantInterval multiply(const ConstantInterval &a, int64_t b) {
    ConstantInterval result(a);
    if (b < 0) {
        result = negate(result);
        b = -b;
    }
    if (result.has_lower_bound()) {
        result.min *= b;
    }
    if (result.has_upper_bound()) {
        result.max *= b;
    }
    return result;
}

ConstantInterval multiply(const ConstantInterval &a, const ConstantInterval &b) {
    std::vector<int64_t> bounds;
    bounds.reserve(4);
    ConstantInterval result;
    result.min_defined = result.max_defined = true;
    if (a.has_lower_bound() && b.has_lower_bound()) {
        bounds.push_back(a.min * b.min);
    } else {
        result.max_defined = false;
    }
    if (a.has_lower_bound() && b.has_upper_bound()) {
        bounds.push_back(a.min * b.max);
    } else {
        result.min_defined = false;
    }
    if (a.has_upper_bound() && b.has_lower_bound()) {
        bounds.push_back(a.max * b.min);
    } else {
        result.min_defined = false;
    }
    if (a.has_upper_bound() && b.has_upper_bound()) {
        bounds.push_back(a.max * b.max);
    } else {
        result.max_defined = false;
    }
    if (!bounds.empty()) {
        result.min = *std::min_element(bounds.begin(), bounds.end());
        result.max = *std::max_element(bounds.begin(), bounds.end());
    }
    return result;
}

ConstantInterval divide(const ConstantInterval &a, int64_t b) {
    ConstantInterval result(a);
    if (b < 0) {
        result = negate(result);
        b = -b;
    }
    if (result.has_lower_bound()) {
        result.min = div_imp(result.min, b);
    }
    if (result.has_upper_bound()) {
        result.max = div_imp(result.max + b - 1, b);
    }
    return result;
}

class DerivativeBounds : public IRVisitor {
    const string &var;

    Scope<ConstantInterval> scope;

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
            result = ConstantInterval();
        }
    }

    void visit(const Variable *op) override {
        if (op->name == var) {
            result = ConstantInterval::single_point(1);
        } else if (scope.contains(op->name)) {
            result = scope.get(op->name);
        } else {
            result = ConstantInterval::single_point(0);
        }
    }

    void visit(const Add *op) override {
        op->a.accept(this);
        ConstantInterval ra = result;
        op->b.accept(this);
        ConstantInterval rb = result;
        result = add(ra, rb);
    }

    void visit(const Sub *op) override {
        op->a.accept(this);
        ConstantInterval ra = result;
        op->b.accept(this);
        ConstantInterval rb = result;
        result = sub(ra, rb);
    }

    void visit(const Mul *op) override {
        if (op->type.is_scalar()) {
            op->a.accept(this);
            ConstantInterval ra = result;
            op->b.accept(this);
            ConstantInterval rb = result;

            if (const int64_t *b = as_const_int(op->b)) {
                result = multiply(ra, *b);
            } else if (const uint64_t *b = as_const_uint(op->b)) {
                result = multiply(ra, *b);
            } else if (const int64_t *a = as_const_int(op->a)) {
                result = multiply(rb, *a);
            } else if (const uint64_t *a = as_const_uint(op->a)) {
                result = multiply(rb, *a);
            } else {
                result = ConstantInterval();
            }
        } else {
            result = ConstantInterval();
        }
    }

    void visit(const Div *op) override {
        if (op->type.is_scalar()) {
            op->a.accept(this);
            ConstantInterval ra = result;
            op->b.accept(this);
            ConstantInterval rb = result;

            if (const int64_t *b = as_const_int(op->b)) {
                result = divide(ra, *b);
            } else if (const uint64_t *b = as_const_uint(op->b)) {
                result = divide(ra, *b);
            } else if (const int64_t *a = as_const_int(op->a)) {
                result = divide(rb, *a);
            } else if (const uint64_t *a = as_const_uint(op->a)) {
                result = divide(rb, *a);
            } else {
                result = ConstantInterval();
            }
        } else {
            result = ConstantInterval();
        }
    }

    void visit(const Mod *op) override {
        result = ConstantInterval();
    }

    void visit(const Min *op) override {
        op->a.accept(this);
        ConstantInterval ra = result;
        op->b.accept(this);
        ConstantInterval rb = result;
        result = unify(ra, rb);
    }

    void visit(const Max *op) override {
        op->a.accept(this);
        ConstantInterval ra = result;
        op->b.accept(this);
        ConstantInterval rb = result;
        result = unify(ra, rb);
    }

    void visit_eq(const Expr &a, const Expr &b) {
        a.accept(this);
        ConstantInterval ra = result;
        b.accept(this);
        ConstantInterval rb = result;
        if (is_constant(ra) && is_constant(rb)) {
            result = ConstantInterval::single_point(0);
        } else {
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
        ConstantInterval rb = result;
        result = unify(negate(ra), rb);
        if (result.has_lower_bound()) {
            result.min = std::max<int64_t>(result.min, -1);
        }
        if (result.has_upper_bound()) {
            result.max = std::min<int64_t>(result.max, 1);
        }
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
        ConstantInterval rb = result;
        result = unify(ra, rb);
    }

    void visit(const Or *op) override {
        op->a.accept(this);
        ConstantInterval ra = result;
        op->b.accept(this);
        ConstantInterval rb = result;
        result = unify(ra, rb);
    }

    void visit(const Not *op) override {
        op->a.accept(this);
        result = negate(result);
    }

    void visit(const Select *op) override {
        // The result is the unified bounds, added to the "bump" that happens when switching from true to false.
        if (op->type.is_scalar()) {
            op->condition.accept(this);
            ConstantInterval rcond = result;

            op->true_value.accept(this);
            ConstantInterval ra = result;
            op->false_value.accept(this);
            ConstantInterval rb = result;
            ConstantInterval unified = unify(ra, rb);

            // TODO: How to handle unsigned values?
            Expr delta = simplify(op->true_value - op->false_value);
            delta.accept(this);
            ConstantInterval rdelta = result;

            ConstantInterval adjusted_delta;
            if (const int64_t *const_delta = as_const_int(delta)) {
                adjusted_delta = multiply(rcond, *const_delta);
            } else {
                adjusted_delta = multiply(rcond, rdelta);
            }
            result = add(unified, adjusted_delta);
        } else {
            result = ConstantInterval();
        }
    }

    void visit(const Load *op) override {
        op->index.accept(this);
        if (!is_constant(result)) {
            result = ConstantInterval();
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
            result = ConstantInterval();
            return;
        }

        for (size_t i = 0; i < op->args.size(); i++) {
            op->args[i].accept(this);
            if (!is_constant(result)) {
                // One of the args is not constant.
                result = ConstantInterval();
                return;
            }
        }
        result = ConstantInterval::single_point(0);
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
                result = ConstantInterval();
                return;
            }
        }
        result = ConstantInterval::single_point(0);
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
                result = ConstantInterval();
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
    ConstantInterval result;

    DerivativeBounds(const std::string &v, const Scope<ConstantInterval> &parent)
        : var(v), result(ConstantInterval()) {
        scope.set_containing_scope(&parent);
    }
};

}  // namespace

ConstantInterval derivative_bounds(const Expr &e, const std::string &var, const Scope<ConstantInterval> &scope) {
    if (!e.defined()) {
        return ConstantInterval();
    }
    DerivativeBounds m(var, scope);
    e.accept(&m);
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

Monotonic is_monotonic_strong(const Expr &e, const std::string &var) {
    return is_monotonic(e, var, Scope<ConstantInterval>());
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
    check_decreasing(-min(x, 16));

    std::cout << "is_monotonic test passed" << std::endl;
}

}  // namespace Internal
}  // namespace Halide

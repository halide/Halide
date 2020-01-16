#include <iostream>

#include "Bounds.h"
#include "CSE.h"
#include "ConciseCasts.h"
#include "Debug.h"
#include "Deinterleave.h"
#include "ExprUsesVar.h"
#include "Func.h"
#include "IR.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "IRVisitor.h"
#include "InlineReductions.h"
#include "Param.h"
#include "PurifyIndexMath.h"
#include "Simplify.h"
#include "Solve.h"
#include "Util.h"
#include "Var.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {
int static_sign(Expr x) {
    if (is_positive_const(x)) {
        return 1;
    } else if (is_negative_const(x)) {
        return -1;
    } else {
        Expr zero = make_zero(x.type());
        if (equal(const_true(), simplify(x > zero))) {
            return 1;
        } else if (equal(const_true(), simplify(x < zero))) {
            return -1;
        }
    }
    return 0;
}
}  // anonymous namespace

Expr find_constant_bound(const Expr &e, Direction d, const Scope<Interval> &scope) {
    Interval interval = find_constant_bounds(e, scope);
    Expr bound;
    if (interval.has_lower_bound() && (d == Direction::Lower)) {
        bound = interval.min;
    } else if (interval.has_upper_bound() && (d == Direction::Upper)) {
        bound = interval.max;
    }
    return bound;
}

Interval find_constant_bounds(const Expr &e, const Scope<Interval> &scope) {
    Interval interval = bounds_of_expr_in_scope(e, scope, FuncValueBounds(), true);
    interval.min = simplify(interval.min);
    interval.max = simplify(interval.max);

    // Note that we can get non-const but well-defined results (e.g. signed_integer_overflow);
    // for our purposes here, treat anything non-const as no-bound.
    if (!is_const(interval.min)) interval.min = Interval::neg_inf();
    if (!is_const(interval.max)) interval.max = Interval::pos_inf();

    return interval;
}

class Bounds : public IRVisitor {
public:
    Interval interval;
    Scope<Interval> scope;
    const FuncValueBounds &func_bounds;
    // If set to true, attempt to return an interval with constant upper
    // and lower bounds. If the bound is not constant, it is set to
    // unbounded.
    bool const_bound;

    Bounds(const Scope<Interval> *s, const FuncValueBounds &fb, bool const_bound)
        : func_bounds(fb), const_bound(const_bound) {
        scope.set_containing_scope(s);
    }

private:
#ifndef DO_TRACK_BOUNDS_INTERVALS
#define DO_TRACK_BOUNDS_INTERVALS 0
#endif

#if DO_TRACK_BOUNDS_INTERVALS

    static int &get_logging() {
        static int do_log = 1;
        return do_log;
    }

    int interval_log_indent = 0;

    void log_interval(const std::string &msg) const {
        if (get_logging()) {
            std::string spaces(interval_log_indent, ' ');
            debug(0) << spaces << msg << "\n"
                     << spaces << "  mn=" << interval.min << "\n"
                     << spaces << "  mx=" << interval.max << "\n";
        }
    }

    void log_interval_msg(const std::string &msg) {
        if (get_logging()) {
            std::string spaces(interval_log_indent, ' ');
            debug(0) << spaces << msg << "\n";
        }
    }

    struct IntervalLogger {
        Bounds *self;
        std::string name;
        IntervalLogger(Bounds *self, const char *pretty_function)
            : self(self) {
            name = replace_all(pretty_function, "virtual void Halide::Internal::", "");
            name = replace_all(name, "(const Halide::Internal::", "(");
            self->log_interval_msg("Enter " + name);
            self->interval_log_indent++;
        }
        ~IntervalLogger() {
            self->interval_log_indent--;
            self->log_interval("Exit  " + name);
        }
    };

#define TRACK_BOUNDS_INTERVAL IntervalLogger log_me_here_(this, __PRETTY_FUNCTION__)

#else

#define TRACK_BOUNDS_INTERVAL \
    do {                      \
    } while (0)

#endif

    // Compute the intrinsic bounds of a function.
    void bounds_of_func(string name, int value_index, Type t) {
        // if we can't get a good bound from the function, fall back to the bounds of the type.
        bounds_of_type(t);

        pair<string, int> key = {name, value_index};

        FuncValueBounds::const_iterator iter = func_bounds.find(key);

        if (iter != func_bounds.end()) {
            if (iter->second.has_lower_bound()) {
                interval.min = iter->second.min;
            }
            if (iter->second.has_upper_bound()) {
                interval.max = iter->second.max;
            }
        }
    }

    void bounds_of_type(Type t) {
        t = t.element_of();
        if ((t.is_uint() || t.is_int()) && t.bits() <= 16) {
            interval = Interval(t.min(), t.max());
        } else {
            interval = Interval::everything();
        }
    }

    using IRVisitor::visit;

    void visit(const IntImm *op) override {
        TRACK_BOUNDS_INTERVAL;
        interval = Interval::single_point(op);
    }

    void visit(const UIntImm *op) override {
        TRACK_BOUNDS_INTERVAL;
        interval = Interval::single_point(op);
    }

    void visit(const FloatImm *op) override {
        TRACK_BOUNDS_INTERVAL;
        interval = Interval::single_point(op);
    }

    void visit(const StringImm *op) override {
        TRACK_BOUNDS_INTERVAL;
        interval = Interval::single_point(op);
    }

    void visit(const Cast *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->value.accept(this);
        Interval a = interval;

        if (a.is_single_point(op->value)) {
            interval = Interval::single_point(op);
            return;
        }

        Type to = op->type.element_of();
        Type from = op->value.type().element_of();

        if (a.is_single_point()) {
            interval = Interval::single_point(Cast::make(to, a.min));
            return;
        }

        // If overflow is impossible, cast the min and max. If it's
        // possible, use the bounds of the destination type.
        bool could_overflow = true;
        if (to.can_represent(from) || to.is_float()) {
            could_overflow = false;
        } else if (to.is_int() && to.bits() >= 32) {
            // If we cast to an int32 or greater, assume that it won't
            // overflow. Signed 32-bit integer overflow is undefined.
            could_overflow = false;
        } else if (a.is_bounded()) {
            if (from.can_represent(to)) {
                // The other case to consider is narrowing where the
                // bounds of the original fit into the narrower type. We
                // can only really prove that this is the case if they're
                // constants, so try to make the constants first.

                // First constant-fold
                a.min = simplify(a.min);
                a.max = simplify(a.max);

                // Then try to strip off junk mins and maxes.
                bool old_constant_bound = const_bound;
                const_bound = true;
                a.min.accept(this);
                Expr lower_bound = interval.has_lower_bound() ? interval.min : Expr();
                a.max.accept(this);
                Expr upper_bound = interval.has_upper_bound() ? interval.max : Expr();
                const_bound = old_constant_bound;

                if (lower_bound.defined() && upper_bound.defined()) {
                    // Cast them to the narrow type and back and see if
                    // they're provably unchanged.
                    Expr test =
                        (cast(from, cast(to, lower_bound)) == lower_bound &&
                         cast(from, cast(to, upper_bound)) == upper_bound);
                    if (can_prove(test)) {
                        could_overflow = false;
                        // Relax the bounds to the constants we found. Not
                        // strictly necessary, but probably helpful to
                        // keep the expressions small.
                        a = Interval(lower_bound, upper_bound);
                    }
                }
            } else {
                // a is bounded, but from and to can't necessarily represent
                // each other; however, if the bounds can be simplified to
                // constants, they might fit regardless of types.
                a.min = simplify(a.min);
                a.max = simplify(a.max);
                auto *umin = as_const_uint(a.min);
                auto *umax = as_const_uint(a.max);
                if (umin && umax && to.can_represent(*umin) && to.can_represent(*umax)) {
                    could_overflow = false;
                } else {
                    auto *imin = as_const_int(a.min);
                    auto *imax = as_const_int(a.max);
                    if (imin && imax && to.can_represent(*imin) && to.can_represent(*imax)) {
                        could_overflow = false;
                    } else {
                        auto *fmin = as_const_float(a.min);
                        auto *fmax = as_const_float(a.max);
                        if (fmin && fmax && to.can_represent(*fmin) && to.can_represent(*fmax)) {
                            could_overflow = false;
                        }
                    }
                }
            }
        }

        if (!could_overflow) {
            // Start with the bounds of the narrow type.
            bounds_of_type(from);
            // If we have a better min or max for the arg use that.
            if (a.has_lower_bound()) interval.min = a.min;
            if (a.has_upper_bound()) interval.max = a.max;
            // Then cast those bounds to the wider type.
            if (interval.has_lower_bound()) interval.min = Cast::make(to, interval.min);
            if (interval.has_upper_bound()) interval.max = Cast::make(to, interval.max);
        } else {
            // This might overflow, so use the bounds of the destination type.
            bounds_of_type(to);
        }
    }

    void visit(const Variable *op) override {
        TRACK_BOUNDS_INTERVAL;
        if (const_bound) {
            bounds_of_type(op->type);
            if (scope.contains(op->name)) {
                const Interval &scope_interval = scope.get(op->name);
                if (scope_interval.has_upper_bound() && is_const(scope_interval.max)) {
                    interval.max = Interval::make_min(interval.max, scope_interval.max);
                }
                if (scope_interval.has_lower_bound() && is_const(scope_interval.min)) {
                    interval.min = Interval::make_max(interval.min, scope_interval.min);
                }
            }

            if (op->param.defined() &&
                !op->param.is_buffer() &&
                (op->param.min_value().defined() ||
                 op->param.max_value().defined())) {

                if (op->param.max_value().defined() && is_const(op->param.max_value())) {
                    interval.max = Interval::make_min(interval.max, op->param.max_value());
                }
                if (op->param.min_value().defined() && is_const(op->param.min_value())) {
                    interval.min = Interval::make_max(interval.min, op->param.min_value());
                }
            }
        } else {
            if (scope.contains(op->name)) {
                interval = scope.get(op->name);
            } else if (op->type.is_vector()) {
                // Uh oh, we need to take the min/max lane of some unknown vector. Treat as unbounded.
                bounds_of_type(op->type);
            } else {
                interval = Interval::single_point(op);
            }
        }
    }

    void visit(const Add *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->a.accept(this);
        Interval a = interval;
        op->b.accept(this);
        Interval b = interval;

        if (a.is_single_point(op->a) && b.is_single_point(op->b)) {
            interval = Interval::single_point(op);
        } else if (a.is_single_point() && b.is_single_point()) {
            interval = Interval::single_point(a.min + b.min);
        } else {
            interval = Interval::everything();
            if (a.has_lower_bound() && b.has_lower_bound()) {
                interval.min = a.min + b.min;
            }
            if (a.has_upper_bound() && b.has_upper_bound()) {
                interval.max = a.max + b.max;
            }

            // Assume no overflow for float, int32, and int64
            if (!op->type.is_float() && (!op->type.is_int() || op->type.bits() < 32)) {
                if (interval.has_upper_bound()) {
                    Expr no_overflow = (cast<int>(a.max) + cast<int>(b.max) == cast<int>(interval.max));
                    if (!can_prove(no_overflow)) {
                        bounds_of_type(op->type);
                        return;
                    }
                }
                if (interval.has_lower_bound()) {
                    Expr no_overflow = (cast<int>(a.min) + cast<int>(b.min) == cast<int>(interval.min));
                    if (!can_prove(no_overflow)) {
                        bounds_of_type(op->type);
                        return;
                    }
                }
            }
        }
    }

    void visit(const Sub *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->a.accept(this);
        Interval a = interval;
        op->b.accept(this);
        Interval b = interval;

        if (a.is_single_point(op->a) && b.is_single_point(op->b)) {
            interval = Interval::single_point(op);
        } else if (a.is_single_point() && b.is_single_point()) {
            interval = Interval::single_point(a.min - b.min);
        } else {
            interval = Interval::everything();
            if (a.has_lower_bound() && b.has_upper_bound()) {
                interval.min = a.min - b.max;
            }
            if (a.has_upper_bound() && b.has_lower_bound()) {
                interval.max = a.max - b.min;
            }

            // Assume no overflow for float, int32, and int64
            if (!op->type.is_float() && (!op->type.is_int() || op->type.bits() < 32)) {
                if (interval.has_upper_bound()) {
                    Expr no_overflow = (cast<int>(a.max) - cast<int>(b.min) == cast<int>(interval.max));
                    if (!can_prove(no_overflow)) {
                        bounds_of_type(op->type);
                        return;
                    }
                }
                if (interval.has_lower_bound()) {
                    Expr no_overflow = (cast<int>(a.min) - cast<int>(b.max) == cast<int>(interval.min));
                    if (!can_prove(no_overflow)) {
                        bounds_of_type(op->type);
                        return;
                    }
                }
            }

            // Check underflow for uint
            if (op->type.is_uint() &&
                interval.has_lower_bound() &&
                !can_prove(b.max <= a.min)) {
                bounds_of_type(op->type);
            }
        }
    }

    void visit(const Mul *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->a.accept(this);
        Interval a = interval;

        op->b.accept(this);
        Interval b = interval;

        // Move constants to the right
        if (a.is_single_point() && !b.is_single_point()) {
            std::swap(a, b);
        }

        if (a.is_single_point(op->a) && b.is_single_point(op->b)) {
            interval = Interval::single_point(op);
            return;
        } else if (a.is_single_point() && b.is_single_point()) {
            interval = Interval::single_point(a.min * b.min);
            return;
        } else if (b.is_single_point()) {
            Expr e1 = a.has_lower_bound() ? a.min * b.min : a.min;
            Expr e2 = a.has_upper_bound() ? a.max * b.min : a.max;
            if (is_zero(b.min)) {
                interval = b;
            } else if (is_positive_const(b.min) || op->type.is_uint()) {
                interval = Interval(e1, e2);
            } else if (is_negative_const(b.min)) {
                if (e1.same_as(Interval::neg_inf())) {
                    e1 = Interval::pos_inf();
                }
                if (e2.same_as(Interval::pos_inf())) {
                    e2 = Interval::neg_inf();
                }
                interval = Interval(e2, e1);
            } else if (a.is_bounded()) {
                // Sign of b is unknown
                Expr cmp = b.min >= make_zero(b.min.type().element_of());
                interval = Interval(select(cmp, e1, e2), select(cmp, e2, e1));
            } else {
                interval = Interval::everything();
            }
        } else if (a.is_bounded() && b.is_bounded()) {
            interval = Interval::nothing();
            interval.include(a.min * b.min);
            interval.include(a.min * b.max);
            interval.include(a.max * b.min);
            interval.include(a.max * b.max);
        } else {
            interval = Interval::everything();
        }

        // Assume no overflow for float, int32, and int64
        if (!op->type.is_float() && (!op->type.is_int() || op->type.bits() < 32)) {
            if (a.is_bounded() && b.is_bounded()) {
                // Try to prove it can't overflow. (Be sure to use uint32 for unsigned
                // types so that the case of 65535*65535 won't misleadingly fail.)
                Type t = op->type.is_uint() ? UInt(32) : Int(32);
                Expr test1 = (cast(t, a.min) * cast(t, b.min) == cast(t, a.min * b.min));
                Expr test2 = (cast(t, a.min) * cast(t, b.max) == cast(t, a.min * b.max));
                Expr test3 = (cast(t, a.max) * cast(t, b.min) == cast(t, a.max * b.min));
                Expr test4 = (cast(t, a.max) * cast(t, b.max) == cast(t, a.max * b.max));
                if (!can_prove(test1 && test2 && test3 && test4)) {
                    bounds_of_type(op->type);
                }
            } else {
                bounds_of_type(op->type);
            }
        }
    }

    void visit(const Div *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->a.accept(this);
        Interval a = interval;

        op->b.accept(this);
        Interval b = interval;

        if (!b.is_bounded()) {
            // Integer division can only make things smaller in
            // magnitude (but can flip the sign).
            if (a.is_bounded() && op->type.is_int() && op->type.bits() >= 32) {
                // Restrict to no-overflow types to avoid worrying
                // about overflow due to negating the most negative int.
                if (can_prove(a.min >= 0)) {
                    interval.min = -a.max;
                    interval.max = a.max;
                } else if (can_prove(a.max <= 0)) {
                    interval.min = a.min;
                    interval.max = -a.min;
                } else if (a.is_single_point()) {
                    // The following case would also be correct, but
                    // would duplicate the expression, which is
                    // generally a bad thing for any later interval
                    // arithmetic.
                    interval.min = -cast(a.min.type(), abs(a.min));
                    interval.max = cast(a.min.type(), abs(a.max));
                } else {
                    interval.min = min(-a.max, a.min);
                    interval.max = max(-a.max, a.min);
                }
            } else {
                interval = Interval::everything();
            }
        } else if (a.is_single_point(op->a) && b.is_single_point(op->b)) {
            interval = Interval::single_point(op);
        } else if (can_prove(b.min == b.max)) {
            Expr e1 = a.has_lower_bound() ? a.min / b.min : a.min;
            Expr e2 = a.has_upper_bound() ? a.max / b.max : a.max;
            if (is_positive_const(b.min) || op->type.is_uint()) {
                interval = Interval(e1, e2);
            } else if (is_negative_const(b.min)) {
                if (e1.same_as(Interval::neg_inf())) {
                    e1 = Interval::pos_inf();
                }
                if (e2.same_as(Interval::pos_inf())) {
                    e2 = Interval::neg_inf();
                }
                interval = Interval(e2, e1);
            } else if (a.is_bounded()) {
                // Sign of b is unknown.
                Expr cmp = b.min > make_zero(b.min.type().element_of());
                interval = Interval(select(cmp, e1, e2), select(cmp, e2, e1));
            } else {
                interval = Interval::everything();
            }
        } else if (a.is_bounded()) {
            // if we can't statically prove that the divisor can't span zero, then we're unbounded
            int min_sign = static_sign(b.min);
            int max_sign = static_sign(b.max);
            if (min_sign != max_sign || min_sign == 0 || max_sign == 0) {
                interval = Interval::everything();
            } else {
                // Divisor is either strictly positive or strictly
                // negative, so we can just take the extrema.
                interval = Interval::nothing();
                interval.include(a.min / b.min);
                interval.include(a.max / b.min);
                interval.include(a.min / b.max);
                interval.include(a.max / b.max);
            }
        } else {
            interval = Interval::everything();
        }
    }

    void visit(const Mod *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->a.accept(this);
        Interval a = interval;

        op->b.accept(this);
        Interval b = interval;

        if (a.is_single_point(op->a) && b.is_single_point(op->b)) {
            interval = Interval::single_point(op);
            return;
        }

        Type t = op->type.element_of();

        // Mod is always positive
        interval.min = make_zero(t);
        interval.max = Interval::pos_inf();

        if (!b.is_bounded()) {
            if (a.has_lower_bound() && can_prove(a.min >= 0)) {
                // Mod cannot positive values larger
                interval.max = a.max;
            }
        } else {
            // b is bounded
            if (b.max.type().is_uint() || (b.max.type().is_int() && is_positive_const(b.min))) {
                // If the RHS is a positive integer, the result is in [0, max_b-1]
                interval.max = Max::make(interval.min, b.max - make_one(t));
            } else if (b.max.type().is_int()) {
                // x % [4,10] -> [0,9]
                // x % [-8,-3] -> [0,7]
                // x % [-8, 10] -> [0,9]
                interval.max = Max::make(interval.min, b.max - make_one(t));
                interval.max = Max::make(interval.max, make_const(t, -1) - b.min);
            } else if (b.max.type().is_float()) {
                // The floating point version has the same sign rules,
                // but can reach all the way up to the original value,
                // so there's no -1.
                interval.max = Max::make(b.max, -b.min);
            }
        }
    }

    void visit(const Min *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->a.accept(this);
        Interval a = interval;

        op->b.accept(this);
        Interval b = interval;

        if (a.is_single_point(op->a) && b.is_single_point(op->b)) {
            interval = Interval::single_point(op);
        } else {
            interval = Interval(Interval::make_min(a.min, b.min),
                                Interval::make_min(a.max, b.max));
        }
    }

    void visit(const Max *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->a.accept(this);
        Interval a = interval;

        op->b.accept(this);
        Interval b = interval;

        if (a.is_single_point(op->a) && b.is_single_point(op->b)) {
            interval = Interval::single_point(op);
        } else {
            interval = Interval(Interval::make_max(a.min, b.min),
                                Interval::make_max(a.max, b.max));
        }
    }

    template<typename Cmp>
    void visit_compare(const Expr &a_expr, const Expr &b_expr) {
        a_expr.accept(this);
        if (!interval.is_bounded()) {
            bounds_of_type(Bool());
            return;
        }
        Interval a = interval;

        b_expr.accept(this);
        if (!interval.is_bounded()) {
            bounds_of_type(Bool());
            return;
        }
        Interval b = interval;

        // The returned interval should have the property that min <=
        // val <= max. For integers it's clear what this means. For
        // bools, treating false < true, '<=' is in fact
        // implication. So we want conditions min and max such that
        // min implies val implies max.  So min should be a sufficient
        // condition, and max should be a necessary condition.

        interval.min = Cmp::make(a.max, b.min);
        interval.max = Cmp::make(a.min, b.max);
    }

    void visit(const LT *op) override {
        TRACK_BOUNDS_INTERVAL;
        visit_compare<LT>(op->a, op->b);
    }

    void visit(const LE *op) override {
        TRACK_BOUNDS_INTERVAL;
        visit_compare<LE>(op->a, op->b);
    }

    void visit(const GT *op) override {
        TRACK_BOUNDS_INTERVAL;
        visit_compare<LT>(op->b, op->a);
    }

    void visit(const GE *op) override {
        TRACK_BOUNDS_INTERVAL;
        visit_compare<LE>(op->b, op->a);
    }

    void visit(const EQ *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->a.accept(this);
        Interval a = interval;

        op->b.accept(this);
        Interval b = interval;

        if (a.is_single_point(op->a) && b.is_single_point(op->b)) {
            interval = Interval::single_point(op);
        } else if (a.is_single_point() && b.is_single_point()) {
            interval = Interval::single_point(a.min == b.min);
        } else {
            // If either vary, it could always be false, so we have no
            // good sufficient condition.
            bounds_of_type(op->type);
            // But could it be true? A necessary condition is that the
            // ranges overlap.
            if (a.is_bounded() && b.is_bounded()) {
                interval.max = a.min <= b.max && b.min <= a.max;
            }
        }
    }

    void visit(const NE *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->a.accept(this);
        Interval a = interval;

        op->b.accept(this);
        Interval b = interval;

        if (a.is_single_point(op->a) && b.is_single_point(op->b)) {
            interval = Interval::single_point(op);
        } else if (a.is_single_point() && b.is_single_point()) {
            interval = Interval::single_point(a.min != b.min);
        } else {
            // If either vary, it could always be true that they're
            // not equal, so we have no good necessary condition.
            bounds_of_type(op->type);
            // But we do have a sufficient condition. If the ranges of
            // a and b do not overlap, then they must be not equal.
            if (a.is_bounded() && b.is_bounded()) {
                interval.min = a.min > b.max || b.min > a.max;
            }
        }
    }

    Expr make_and(Expr a, Expr b) {
        if (is_one(a)) return b;
        if (is_one(b)) return a;
        if (is_zero(a)) return a;
        if (is_zero(b)) return b;
        return a && b;
    }

    void visit(const And *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->a.accept(this);
        Interval a = interval;

        op->b.accept(this);
        Interval b = interval;

        if (a.is_single_point(op->a) && b.is_single_point(op->b)) {
            interval = Interval::single_point(op);
        } else if (a.is_single_point() && b.is_single_point()) {
            interval = Interval::single_point(a.min && b.min);
        } else {
            // And is monotonic increasing in both args
            interval.min = make_and(a.min, b.min);
            interval.max = make_and(a.max, b.max);
        }
    }

    Expr make_or(Expr a, Expr b) {
        if (is_one(a)) return a;
        if (is_one(b)) return b;
        if (is_zero(a)) return b;
        if (is_zero(b)) return a;
        return a || b;
    }

    void visit(const Or *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->a.accept(this);
        Interval a = interval;

        op->b.accept(this);
        Interval b = interval;

        if (a.is_single_point(op->a) && b.is_single_point(op->b)) {
            interval = Interval::single_point(op);
        } else if (a.is_single_point() && b.is_single_point()) {
            interval = Interval::single_point(a.min || b.min);
        } else {
            // Or is monotonic increasing in both args
            interval.min = make_or(a.min, b.min);
            interval.max = make_or(a.max, b.max);
        }
    }

    Expr make_not(Expr e) {
        if (is_one(e)) return make_zero(e.type());
        if (is_zero(e)) return make_one(e.type());
        return !e;
    }

    void visit(const Not *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->a.accept(this);
        Interval a = interval;

        if (a.is_single_point(op->a)) {
            interval = Interval::single_point(op);
        } else if (a.is_single_point()) {
            interval = Interval::single_point(!a.min);
        } else {
            interval.min = make_not(a.max);
            interval.max = make_not(a.min);
        }
    }

    void visit(const Select *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->true_value.accept(this);
        Interval a = interval;

        op->false_value.accept(this);
        Interval b = interval;

        op->condition.accept(this);
        Interval cond = interval;

        if (cond.is_single_point()) {
            if (is_one(cond.min)) {
                interval = a;
                return;
            } else if (is_zero(cond.min)) {
                interval = b;
                return;
            }
        }

        Type t = op->type.element_of();

        if (!a.has_lower_bound() || !b.has_lower_bound()) {
            interval.min = Interval::neg_inf();
        } else if (a.min.same_as(b.min)) {
            interval.min = a.min;
        } else if (cond.is_single_point()) {
            interval.min = select(cond.min, a.min, b.min);
        } else if (is_zero(cond.min) && is_one(cond.max)) {
            interval.min = Interval::make_min(a.min, b.min);
        } else if (is_one(cond.max)) {
            // cond.min is non-trivial
            string var_name = unique_name('t');
            Expr var = Variable::make(t, var_name);
            interval.min = Interval::make_min(select(cond.min, var, b.min), var);
            interval.min = Let::make(var_name, a.min, interval.min);
        } else if (is_zero(cond.min)) {
            // cond.max is non-trivial
            string var_name = unique_name('t');
            Expr var = Variable::make(t, var_name);
            interval.min = Interval::make_min(select(cond.max, a.min, var), var);
            interval.min = Let::make(var_name, b.min, interval.min);
        } else {
            string a_var_name = unique_name('t'), b_var_name = unique_name('t');
            Expr a_var = Variable::make(t, a_var_name);
            Expr b_var = Variable::make(t, b_var_name);
            interval.min = Interval::make_min(select(cond.min, a_var, b_var),
                                              select(cond.max, a_var, b_var));
            interval.min = Let::make(a_var_name, a.min, interval.min);
            interval.min = Let::make(b_var_name, b.min, interval.min);
        }

        if (!a.has_upper_bound() || !b.has_upper_bound()) {
            interval.max = Interval::pos_inf();
        } else if (a.max.same_as(b.max)) {
            interval.max = a.max;
        } else if (cond.is_single_point()) {
            interval.max = select(cond.min, a.max, b.max);
        } else if (is_zero(cond.min) && is_one(cond.max)) {
            interval.max = Interval::make_max(a.max, b.max);
        } else if (is_one(cond.max)) {
            // cond.min is non-trivial
            string var_name = unique_name('t');
            Expr var = Variable::make(t, var_name);
            interval.max = Interval::make_max(select(cond.min, var, b.max), var);
            interval.max = Let::make(var_name, a.max, interval.max);
        } else if (is_zero(cond.min)) {
            // cond.max is non-trivial
            string var_name = unique_name('t');
            Expr var = Variable::make(t, var_name);
            interval.max = Interval::make_max(select(cond.max, a.max, var), var);
            interval.max = Let::make(var_name, b.max, interval.max);
        } else {
            string a_var_name = unique_name('t'), b_var_name = unique_name('t');
            Expr a_var = Variable::make(t, a_var_name);
            Expr b_var = Variable::make(t, b_var_name);
            interval.max = Interval::make_max(select(cond.min, a_var, b_var),
                                              select(cond.max, a_var, b_var));
            interval.max = Let::make(a_var_name, a.max, interval.max);
            interval.max = Let::make(b_var_name, b.max, interval.max);
        }
    }

    void visit(const Load *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->index.accept(this);
        if (!const_bound && interval.is_single_point() && is_one(op->predicate)) {
            // If the index is const and it is not a predicated load,
            // we can return the load of that index
            Expr load_min =
                Load::make(op->type.element_of(), op->name, interval.min,
                           op->image, op->param, const_true(), ModulusRemainder());
            interval = Interval::single_point(load_min);
        } else {
            // Otherwise use the bounds of the type
            bounds_of_type(op->type);
        }
    }

    void visit(const Ramp *op) override {
        TRACK_BOUNDS_INTERVAL;
        // Treat the ramp lane as a free variable
        string var_name = unique_name('t');
        Expr var = Variable::make(op->base.type(), var_name);
        Expr lane = op->base + var * op->stride;
        ScopedBinding<Interval> p(scope, var_name, Interval(make_const(var.type(), 0), make_const(var.type(), op->lanes - 1)));
        lane.accept(this);
    }

    void visit(const Broadcast *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->value.accept(this);
    }

    void visit(const Call *op) override {
        TRACK_BOUNDS_INTERVAL;
        // Using the strict_float feature flag wraps a strict_float()
        // call around every Expr that is of type float, so it's easy
        // to get nestings that are many levels deep; the bounds of this
        // call are *always* exactly that of its first argument, so short
        // circuit it here before checking for const_args. This is important
        // because evaluating const_args for such a deeply nested case
        // essentially becomes O(n^2) doing work that is unnecessary, making
        // otherwise simple pipelines take several minutes to compile.
        //
        // TODO: are any other intrinsics worth including here as well?
        if (op->is_intrinsic(Call::strict_float)) {
            assert(op->args.size() == 1);
            op->args[0].accept(this);
            return;
        }

        Type t = op->type.element_of();

        if (t.is_handle()) {
            interval = Interval::everything();
            return;
        }

        if (!const_bound &&
            (op->call_type == Call::PureExtern ||
             op->call_type == Call::Image)) {

            // If the args are const we can return the call of those args
            // for pure functions. For other types of functions, the same
            // call in two different places might produce different
            // results (e.g. during the update step of a reduction), so we
            // can't move around call nodes.
            //
            // Note: Only evaluate new_args if we know the call is a candidate;
            // otherwise we can get n^2 evaluation time for deeply-nested
            // Expr trees.

            std::vector<Expr> new_args(op->args.size());
            bool const_args = true;
            for (size_t i = 0; i < op->args.size() && const_args; i++) {
                op->args[i].accept(this);
                if (interval.is_single_point()) {
                    new_args[i] = interval.min;
                } else {
                    const_args = false;
                }
            }
            if (const_args) {
                Expr call = Call::make(t, op->name, new_args, op->call_type,
                                       op->func, op->value_index, op->image, op->param);
                interval = Interval::single_point(call);
                return;
            }
            // else fall thru and continue
        }

        if (op->is_intrinsic(Call::abs)) {
            op->args[0].accept(this);
            Interval a = interval;
            interval.min = make_zero(t);
            if (a.is_bounded()) {
                if (equal(a.min, a.max)) {
                    interval = Interval::single_point(Call::make(t, Call::abs, {a.max}, Call::PureIntrinsic));
                } else if (op->args[0].type().is_int() && op->args[0].type().bits() >= 32) {
                    interval.max = Max::make(Cast::make(t, -a.min), Cast::make(t, a.max));
                } else {
                    a.min = Call::make(t, Call::abs, {a.min}, Call::PureIntrinsic);
                    a.max = Call::make(t, Call::abs, {a.max}, Call::PureIntrinsic);
                    interval.max = Max::make(a.min, a.max);
                }
            } else {
                // If the argument is unbounded on one side, then the max is unbounded.
                interval.max = Interval::pos_inf();
            }
        } else if (op->is_intrinsic(Call::absd)) {
            internal_assert(!t.is_handle());
            if (t.is_float()) {
                Expr e = abs(op->args[0] - op->args[1]);
                e.accept(this);
            } else {
                // absd() for int types will always produce a uint result
                internal_assert(t.is_uint());

                Expr a = op->args[0];
                Expr b = op->args[1];
                internal_assert(a.type() == b.type());

                a.accept(this);
                Interval a_interval = interval;

                b.accept(this);
                Interval b_interval = interval;

                if (a_interval.is_bounded() && b_interval.is_bounded()) {
                    interval.min = make_zero(t);
                    interval.max = max(absd(a_interval.max, b_interval.min), absd(a_interval.min, b_interval.max));
                } else {
                    bounds_of_type(t);
                }
            }
        } else if (op->is_intrinsic(Call::unsafe_promise_clamped)) {
            Expr full_clamp = clamp(op->args[0], op->args[1], op->args[2]);
            full_clamp.accept(this);
        } else if (op->is_intrinsic(Call::likely) ||
                   op->is_intrinsic(Call::likely_if_innermost)) {
            assert(op->args.size() == 1);
            op->args[0].accept(this);
        } else if (op->is_intrinsic(Call::return_second)) {
            assert(op->args.size() == 2);
            op->args[1].accept(this);
        } else if (op->is_intrinsic(Call::if_then_else)) {
            assert(op->args.size() == 3);
            // Probably more conservative than necessary
            Expr equivalent_select = Select::make(op->args[0], op->args[1], op->args[2]);
            equivalent_select.accept(this);
        } else if (op->is_intrinsic(Call::require)) {
            assert(op->args.size() == 3);
            op->args[1].accept(this);
        } else if (op->is_intrinsic(Call::shift_left) ||
                   op->is_intrinsic(Call::shift_right) ||
                   op->is_intrinsic(Call::bitwise_xor) ||
                   op->is_intrinsic(Call::bitwise_and) ||
                   op->is_intrinsic(Call::bitwise_or)) {
            Expr a = op->args[0], b = op->args[1];
            a.accept(this);
            Interval a_interval = interval;
            b.accept(this);
            Interval b_interval = interval;
            if (a_interval.is_single_point(a) && b_interval.is_single_point(b)) {
                interval = Interval::single_point(op);
            } else if (a_interval.is_single_point() && b_interval.is_single_point()) {
                interval = Interval::single_point(Call::make(op->type, op->name, {a_interval.min, b_interval.min}, op->call_type));
            } else {
                bounds_of_type(t);
                // For some of these intrinsics applied to integer
                // types we can go a little further.
                if (t.is_int() || t.is_uint()) {
                    if (op->is_intrinsic(Call::shift_left)) {
                        if (t.is_int() && t.bits() >= 32) {
                            // Overflow is UB
                            if (a_interval.has_lower_bound() &&
                                b_interval.has_lower_bound() &&
                                can_prove(b_interval.min >= 0 &&
                                          b_interval.min < t.bits())) {
                                interval.min = a_interval.min << b_interval.min;
                            } else if (a_interval.has_lower_bound() &&
                                       b_interval.has_lower_bound() &&
                                       !b_interval.min.type().is_uint() &&
                                       can_prove(b_interval.min < 0 &&
                                                 b_interval.min > -t.bits())) {
                                interval.min = a_interval.min >> abs(b_interval.min);
                            }
                            if (a_interval.has_upper_bound() &&
                                b_interval.has_upper_bound() &&
                                can_prove(b_interval.max >= 0 &&
                                          b_interval.max < t.bits())) {
                                interval.max = a_interval.max << b_interval.max;
                            } else if (a_interval.has_upper_bound() &&
                                       b_interval.has_upper_bound() &&
                                       !b_interval.max.type().is_uint() &&
                                       can_prove(b_interval.max < 0 &&
                                                 b_interval.max > -t.bits())) {
                                interval.max = a_interval.max >> abs(b_interval.max);
                            }
                        } else if (is_const(b)) {
                            // We can normalize to multiplication
                            Expr equiv = a * (make_const(t, 1) << b);
                            equiv.accept(this);
                        }
                    } else if (op->is_intrinsic(Call::shift_right)) {
                        // Only try to improve on bounds-of-type if we can prove 0 <= b < t.bits,
                        // as shift_right(a, b) is UB for b outside that range.
                        if (b_interval.is_bounded()) {
                            bool b_min_ok_positive = can_prove(b_interval.min >= 0 &&
                                                               b_interval.min < t.bits());
                            bool b_max_ok_positive = can_prove(b_interval.max >= 0 &&
                                                               b_interval.max < t.bits());
                            bool b_min_ok_negative =
                                !b_interval.min.type().is_uint() &&
                                can_prove(b_interval.min < 0 && b_interval.min > -t.bits());
                            bool b_max_ok_negative =
                                !b_interval.max.type().is_uint() &&
                                can_prove(b_interval.max < 0 && b_interval.max > -t.bits());
                            if (a_interval.has_lower_bound()) {
                                if (can_prove(a_interval.min >= 0) && b_max_ok_positive) {
                                    interval.min = a_interval.min >> b_interval.max;
                                } else if (can_prove(a_interval.min < 0) && b_max_ok_negative) {
                                    interval.min = a_interval.min << abs(b_interval.max);
                                } else if (b_min_ok_positive && b_max_ok_positive) {
                                    // if a < 0, the smallest value will be a >> b.min
                                    // if a > 0, the smallest value will be a >> b.max
                                    interval.min = min(a_interval.min >> b_interval.min,
                                                       a_interval.min >> b_interval.max);
                                } else if (b_min_ok_negative && b_max_ok_negative) {
                                    // if a < 0, the smallest value will be a << abs(b.min)
                                    // if a > 0, the smallest value will be a << abs(b.max)
                                    interval.min = min(a_interval.min << abs(b_interval.min),
                                                       a_interval.min << abs(b_interval.max));
                                }
                            }
                            if (a_interval.has_upper_bound()) {
                                if (can_prove(a_interval.max >= 0) && b_min_ok_positive) {
                                    interval.max = a_interval.max >> b_interval.min;
                                } else if (can_prove(a_interval.max < 0) && b_min_ok_negative) {
                                    interval.max = a_interval.max << abs(b_interval.min);
                                } else if (b_min_ok_positive && b_max_ok_positive) {
                                    // if a < 0, the largest value will be a >> b.max
                                    // if a > 0, the largest value will be a >> b.min
                                    interval.max = max(a_interval.max >> b_interval.max,
                                                       a_interval.max >> b_interval.min);
                                } else if (b_min_ok_negative && b_max_ok_negative) {
                                    // if a < 0, the largest value will be a << abs(b.max)
                                    // if a > 0, the largest value will be a << abs(b.min)
                                    interval.max = max(a_interval.max << abs(b_interval.max),
                                                       a_interval.max << abs(b_interval.min));
                                }
                            }
                        }
                    } else if (op->is_intrinsic(Call::bitwise_and) &&
                               a_interval.has_upper_bound() &&
                               b_interval.has_upper_bound()) {
                        bool a_positive = a_interval.has_lower_bound() && can_prove(a_interval.min >= 0);
                        bool b_positive = b_interval.has_lower_bound() && can_prove(b_interval.min >= 0);
                        if (a_positive && b_positive) {
                            // Positive and smaller than both args
                            interval.max = min(a_interval.max, b_interval.max);
                            interval.min = make_zero(t);
                        } else if (t.is_int()) {
                            if (b_positive) {
                                interval.min = make_zero(t);
                                interval.max = b_interval.max;
                            } else if (a_positive) {
                                interval.min = make_zero(t);
                                interval.max = a_interval.max;
                            } else {
                                // Smaller than the larger of the two args
                                interval.max = max(a_interval.max, b_interval.max);
                            }
                        }

                    } else if (op->is_intrinsic(Call::bitwise_or) &&
                               a_interval.has_lower_bound() &&
                               b_interval.has_lower_bound()) {
                        if (t.is_int()) {
                            // Larger than the smaller arg
                            interval.min = min(a_interval.min, b_interval.min);
                        } else if (t.is_uint()) {
                            // Larger than both args
                            interval.min = max(a_interval.min, b_interval.min);
                        }
                    }
                }
            }
        } else if (op->is_intrinsic(Call::bitwise_not)) {
            // In 2's complement bitwise not inverts the ordering of
            // the space, without causing overflow (unlike negation),
            // so bitwise not is monotonic decreasing.
            op->args[0].accept(this);
            Interval a_interval = interval;
            if (a_interval.is_single_point(op->args[0])) {
                interval = Interval::single_point(op);
            } else if (a_interval.is_single_point()) {
                interval = Interval::single_point(~a_interval.min);
            } else {
                bounds_of_type(t);
                if (t.is_int() || t.is_uint()) {
                    if (a_interval.has_upper_bound()) {
                        interval.min = ~a_interval.max;
                    }
                    if (a_interval.has_lower_bound()) {
                        interval.max = ~a_interval.min;
                    }
                }
            }
        } else if (op->args.size() == 1 && interval.is_bounded() &&
                   (op->name == "ceil_f32" || op->name == "ceil_f64" ||
                    op->name == "floor_f32" || op->name == "floor_f64" ||
                    op->name == "round_f32" || op->name == "round_f64" ||
                    op->name == "exp_f32" || op->name == "exp_f64" ||
                    op->name == "log_f32" || op->name == "log_f64")) {
            // For monotonic, pure, single-argument functions, we can
            // make two calls for the min and the max.
            interval = Interval(
                Call::make(t, op->name, {interval.min}, op->call_type,
                           op->func, op->value_index, op->image, op->param),
                Call::make(t, op->name, {interval.max}, op->call_type,
                           op->func, op->value_index, op->image, op->param));

        } else if (!const_bound &&
                   (op->name == Call::buffer_get_min ||
                    op->name == Call::buffer_get_max)) {
            // Bounds query results should have perfect nesting. Their
            // max over a loop is just the same bounds query call at
            // an outer loop level. This requires that the query is
            // also done at the outer loop level so that the buffer
            // arg is still valid, which it is, so it is.
            //
            // TODO: There should be an assert injected in the inner
            // loop to check perfect nesting.
            interval = Interval(Call::make(Int(32), Call::buffer_get_min, op->args, Call::Extern),
                                Call::make(Int(32), Call::buffer_get_max, op->args, Call::Extern));
        } else if (op->is_intrinsic(Call::popcount) ||
                   op->is_intrinsic(Call::count_leading_zeros) ||
                   op->is_intrinsic(Call::count_trailing_zeros)) {
            internal_assert(op->args.size() == 1);
            const Type &t = op->type.element_of();
            Expr min = make_zero(t);
            Expr max = make_const(t, op->args[0].type().bits());
            if (op->is_intrinsic(Call::count_leading_zeros)) {
                // clz treats signed and unsigned ints the same way;
                // cast all ints to uint to simplify this.
                cast(op->type.with_code(halide_type_uint), op->args[0]).accept(this);
                Interval a = interval;
                if (a.has_lower_bound()) {
                    max = cast(t, count_leading_zeros(a.min));
                }
                if (a.has_upper_bound()) {
                    min = cast(t, count_leading_zeros(a.max));
                }
            }
            interval = Interval(min, max);
        } else if (op->is_intrinsic(Call::memoize_expr)) {
            internal_assert(!op->args.empty());
            op->args[0].accept(this);
        } else if (op->call_type == Call::Halide) {
            bounds_of_func(op->name, op->value_index, op->type);
        } else {
            // Just use the bounds of the type
            bounds_of_type(t);
        }
    }

    void visit(const Let *op) override {
        TRACK_BOUNDS_INTERVAL;
        op->value.accept(this);
        Interval val = interval;

        // We'll either substitute the values in directly, or pass
        // them in as variables and add an outer let (to avoid
        // combinatorial explosion).
        Interval var;
        string min_name = op->name + ".min";
        string max_name = op->name + ".max";

        if (val.has_lower_bound()) {
            if (is_const(val.min)) {
                // Substitute it in
                var.min = val.min;
                val.min = Expr();
            } else {
                var.min = Variable::make(op->value.type().element_of(), min_name);
            }
        }

        if (val.has_upper_bound()) {
            if (is_const(val.max)) {
                // Substitute it in
                var.max = val.max;
                val.max = Expr();
            } else if (val.is_single_point()) {
                var.max = var.min;
            } else {
                var.max = Variable::make(op->value.type().element_of(), max_name);
            }
        }

        {
            ScopedBinding<Interval> p(scope, op->name, var);
            op->body.accept(this);
        }

        bool single_point = interval.is_single_point();

        if (interval.has_lower_bound()) {
            if (val.min.defined() &&
                expr_uses_var(interval.min, min_name)) {
                interval.min = Let::make(min_name, val.min, interval.min);
            }
            if (val.max.defined() &&
                !val.is_single_point() &&
                expr_uses_var(interval.min, max_name)) {
                interval.min = Let::make(max_name, val.max, interval.min);
            }
        }

        if (single_point) {
            interval.max = interval.min;
        } else if (interval.has_upper_bound()) {
            if (val.min.defined() &&
                expr_uses_var(interval.max, min_name)) {
                interval.max = Let::make(min_name, val.min, interval.max);
            }
            if (val.max.defined() &&
                !val.is_single_point() &&
                expr_uses_var(interval.max, max_name)) {
                interval.max = Let::make(max_name, val.max, interval.max);
            }
        }
    }

    void visit(const Shuffle *op) override {
        TRACK_BOUNDS_INTERVAL;
        Interval result = Interval::nothing();
        for (Expr i : op->vectors) {
            i.accept(this);
            result.include(interval);
        }
        interval = result;
    }

    void visit(const LetStmt *) override {
        internal_error << "Bounds of statement\n";
    }

    void visit(const AssertStmt *) override {
        internal_error << "Bounds of statement\n";
    }

    void visit(const ProducerConsumer *) override {
        internal_error << "Bounds of statement\n";
    }

    void visit(const For *) override {
        internal_error << "Bounds of statement\n";
    }

    void visit(const Store *) override {
        internal_error << "Bounds of statement\n";
    }

    void visit(const Provide *) override {
        internal_error << "Bounds of statement\n";
    }

    void visit(const Allocate *) override {
        internal_error << "Bounds of statement\n";
    }

    void visit(const Realize *) override {
        internal_error << "Bounds of statement\n";
    }

    void visit(const Block *) override {
        internal_error << "Bounds of statement\n";
    }
};

Interval bounds_of_expr_in_scope(Expr expr, const Scope<Interval> &scope, const FuncValueBounds &fb, bool const_bound) {
    //debug(3) << "computing bounds_of_expr_in_scope " << expr << "\n";
    Bounds b(&scope, fb, const_bound);
    expr.accept(&b);
    //debug(3) << "bounds_of_expr_in_scope " << expr << " = " << simplify(b.interval.min) << ", " << simplify(b.interval.max) << "\n";
    Type expected = expr.type().element_of();
    if (b.interval.has_lower_bound()) {
        internal_assert(b.interval.min.type() == expected)
            << "Min of " << expr
            << " should have been a scalar of type " << expected
            << ": " << b.interval.min << "\n";
    }
    if (b.interval.has_upper_bound()) {
        internal_assert(b.interval.max.type() == expected)
            << "Max of " << expr
            << " should have been a scalar of type " << expected
            << ": " << b.interval.max << "\n";
    }
    return b.interval;
}

Region region_union(const Region &a, const Region &b) {
    internal_assert(a.size() == b.size()) << "Mismatched dimensionality in region union\n";
    Region result;
    for (size_t i = 0; i < a.size(); i++) {
        Expr min = Min::make(a[i].min, b[i].min);
        Expr max_a = a[i].min + a[i].extent;
        Expr max_b = b[i].min + b[i].extent;
        Expr max_plus_one = Max::make(max_a, max_b);
        Expr extent = max_plus_one - min;
        result.push_back(Range(simplify(min), simplify(extent)));
        //result.push_back(Range(min, extent));
    }
    return result;
}

void merge_boxes(Box &a, const Box &b) {
    if (b.empty()) {
        return;
    }

    if (a.empty()) {
        a = b;
        return;
    }

    internal_assert(a.size() == b.size());

    bool a_maybe_unused = a.maybe_unused();
    bool b_maybe_unused = b.maybe_unused();

    bool complementary = a_maybe_unused && b_maybe_unused &&
                         (equal(a.used, !b.used) || equal(!a.used, b.used));

    for (size_t i = 0; i < a.size(); i++) {
        if (!a[i].min.same_as(b[i].min)) {
            if (a[i].has_lower_bound() && b[i].has_lower_bound()) {
                if (a_maybe_unused && b_maybe_unused) {
                    if (complementary) {
                        a[i].min = select(a.used, a[i].min, b[i].min);
                    } else {
                        a[i].min = select(a.used && b.used, Interval::make_min(a[i].min, b[i].min),
                                          a.used, a[i].min,
                                          b[i].min);
                    }
                } else if (a_maybe_unused) {
                    a[i].min = select(a.used, Interval::make_min(a[i].min, b[i].min), b[i].min);
                } else if (b_maybe_unused) {
                    a[i].min = select(b.used, Interval::make_min(a[i].min, b[i].min), a[i].min);
                } else {
                    a[i].min = Interval::make_min(a[i].min, b[i].min);
                }
                a[i].min = simplify(a[i].min);
            } else {
                a[i].min = Interval::neg_inf();
            }
        }
        if (!a[i].max.same_as(b[i].max)) {
            if (a[i].has_upper_bound() && b[i].has_upper_bound()) {
                if (a_maybe_unused && b_maybe_unused) {
                    if (complementary) {
                        a[i].max = select(a.used, a[i].max, b[i].max);
                    } else {
                        a[i].max = select(a.used && b.used, Interval::make_max(a[i].max, b[i].max),
                                          a.used, a[i].max,
                                          b[i].max);
                    }
                } else if (a_maybe_unused) {
                    a[i].max = select(a.used, Interval::make_max(a[i].max, b[i].max), b[i].max);
                } else if (b_maybe_unused) {
                    a[i].max = select(b.used, Interval::make_max(a[i].max, b[i].max), a[i].max);
                } else {
                    a[i].max = Interval::make_max(a[i].max, b[i].max);
                }
                a[i].max = simplify(a[i].max);
            } else {
                a[i].max = Interval::pos_inf();
            }
        }
    }

    if (a_maybe_unused && b_maybe_unused) {
        if (!equal(a.used, b.used)) {
            a.used = simplify(a.used || b.used);
            if (is_one(a.used)) {
                a.used = Expr();
            }
        }
    } else {
        a.used = Expr();
    }
}

Box box_union(const Box &a, const Box &b) {
    Box result = a;
    merge_boxes(result, b);
    return result;
}

Box box_intersection(const Box &a, const Box &b) {
    Box result;
    if (a.empty() || b.empty()) {
        return result;
    }

    internal_assert(a.size() == b.size());
    result.resize(a.size());

    for (size_t i = 0; i < a.size(); i++) {
        result[i].min = simplify(max(a[i].min, b[i].min));
        result[i].max = simplify(min(a[i].max, b[i].max));
    }

    // The intersection is only used if both a and b are used.
    if (a.maybe_unused() && b.maybe_unused()) {
        result.used = a.used && b.used;
    } else if (a.maybe_unused()) {
        result.used = a.used;
    } else if (b.maybe_unused()) {
        result.used = b.used;
    }

    return result;
}

bool boxes_overlap(const Box &a, const Box &b) {
    // If one box is scalar and the other is not, the boxes cannot
    // overlap.
    if (a.size() != b.size() && (a.empty() || b.empty())) {
        return false;
    }

    internal_assert(a.size() == b.size());

    bool a_maybe_unused = a.maybe_unused();
    bool b_maybe_unused = b.maybe_unused();

    // Overlapping requires both boxes to be used.
    Expr overlap = ((a_maybe_unused ? a.used : const_true()) &&
                    (b_maybe_unused ? b.used : const_true()));

    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].has_upper_bound() && b[i].has_lower_bound()) {
            overlap = overlap && b[i].max >= a[i].min;
        }
        if (a[i].has_lower_bound() && b[i].has_upper_bound()) {
            overlap = overlap && a[i].max >= b[i].min;
        }
    }

    // Conservatively, assume they overlap if we can't prove there's no overlap
    return !can_prove(simplify(!overlap));
}

bool box_contains(const Box &outer, const Box &inner) {
    // If the inner box has more dimensions than the outer box, the
    // inner box cannot fit in the outer box.
    if (inner.size() > outer.size()) {
        return false;
    }
    Expr condition = const_true();
    for (size_t i = 0; i < inner.size(); i++) {
        if ((outer[i].has_lower_bound() && !inner[i].has_lower_bound()) ||
            (outer[i].has_upper_bound() && !inner[i].has_upper_bound())) {
            return false;
        }
        if (outer[i].has_lower_bound()) {
            condition = condition && (outer[i].min <= inner[i].min);
        }
        if (outer[i].has_upper_bound()) {
            condition = condition && (outer[i].max >= inner[i].max);
        }
    }
    if (outer.maybe_unused()) {
        if (inner.maybe_unused()) {
            // inner condition must imply outer one
            condition = condition && ((outer.used && inner.used) == inner.used);
        } else {
            // outer box is conditional, but inner is not
            return false;
        }
    }
    return can_prove(condition);
}

class FindInnermostVar : public IRVisitor {
public:
    const Scope<int> &vars_depth;
    string innermost_var;

    FindInnermostVar(const Scope<int> &vars_depth)
        : vars_depth(vars_depth) {
    }

private:
    using IRVisitor::visit;
    int innermost_depth = -1;

    void visit(const Variable *op) override {
        if (vars_depth.contains(op->name)) {
            int depth = vars_depth.get(op->name);
            if (depth > innermost_depth) {
                innermost_var = op->name;
                innermost_depth = depth;
            }
        }
    }
};

// Place innermost vars in an IfThenElse's condition as far to the left as possible.
class SolveIfThenElse : public IRMutator {
    // Scope of variable names and their depths. Higher depth indicates
    // variable defined more innermost.
    Scope<int> vars_depth;
    int depth = -1;

    using IRMutator::visit;

    void push_var(const string &var) {
        depth += 1;
        vars_depth.push(var, depth);
    }

    void pop_var(const string &var) {
        depth -= 1;
        vars_depth.pop(var);
    }

    Stmt visit(const LetStmt *op) override {
        push_var(op->name);
        Stmt stmt = IRMutator::visit(op);
        pop_var(op->name);
        return stmt;
    }

    Stmt visit(const For *op) override {
        push_var(op->name);
        Stmt stmt = IRMutator::visit(op);
        pop_var(op->name);
        return stmt;
    }

    Stmt visit(const IfThenElse *op) override {
        Stmt stmt = IRMutator::visit(op);
        op = stmt.as<IfThenElse>();
        internal_assert(op);

        FindInnermostVar find(vars_depth);
        op->condition.accept(&find);
        if (!find.innermost_var.empty()) {
            Expr condition = solve_expression(op->condition, find.innermost_var).result;
            if (!condition.same_as(op->condition)) {
                stmt = IfThenElse::make(condition, op->then_case, op->else_case);
            }
        }
        return stmt;
    }
};

// Collect all variables referenced in an expr or statement
// (excluding 'skipped_var')
class CollectVars : public IRGraphVisitor {
public:
    string skipped_var;
    set<string> vars;

    CollectVars(const string &v)
        : skipped_var(v) {
    }

private:
    using IRGraphVisitor::visit;

    void visit(const Variable *op) override {
        if (op->name != skipped_var) {
            vars.insert(op->name);
        }
    }
};

// Compute the box produced by a statement
class BoxesTouched : public IRGraphVisitor {

public:
    BoxesTouched(bool calls, bool provides, string fn, const Scope<Interval> *s, const FuncValueBounds &fb)
        : func(fn), consider_calls(calls), consider_provides(provides), func_bounds(fb) {
        scope.set_containing_scope(s);
    }

    map<string, Box> boxes;

private:
    struct VarInstance {
        string var;
        int instance;
        VarInstance(const string &v, int i)
            : var(v), instance(i) {
        }
        VarInstance() = default;

        bool operator==(const VarInstance &other) const {
            return (var == other.var) && (instance == other.instance);
        }
        bool operator<(const VarInstance &other) const {
            if (var == other.var) {
                return (instance < other.instance);
            }
            return (var < other.var);
        }
    };

    string func;
    bool consider_calls, consider_provides;
    Scope<Interval> scope;
    const FuncValueBounds &func_bounds;
    // Scope containing the current value definition of let stmts.
    Scope<Expr> let_stmts;
    // Keep track of variable renaming. Map variable name to instantiation number
    // (0 for the first variable to be defined, 1 for the 1st redefinition, etc.).
    map<string, int> vars_renaming;
    // Map variable name to all other vars which values depend on that variable.
    map<VarInstance, set<VarInstance>> children;

    bool in_producer{false};
    map<std::string, Expr> buffer_lets;

    using IRGraphVisitor::visit;

    bool box_from_extended_crop(Expr e, Box &b) {
        const Call *call_expr = e.as<Call>();
        if (call_expr != nullptr) {
            if (call_expr->name == Call::buffer_crop) {
                internal_assert(call_expr->args.size() == 5)
                    << "Call::buffer_crop call with unexpected number of arguments.\n";
                const Variable *in_buf = call_expr->args[2].as<Variable>();
                const Call *mins_struct = call_expr->args[3].as<Call>();
                const Call *extents_struct = call_expr->args[4].as<Call>();
                // Ignore crops that apply to a different buffer than the one being looked for.
                if (in_buf != nullptr && (in_buf->name == (func + ".buffer"))) {
                    internal_assert(mins_struct != nullptr && extents_struct != nullptr &&
                                    mins_struct->is_intrinsic(Call::make_struct) &&
                                    extents_struct->is_intrinsic(Call::make_struct))
                        << "BoxesTouched::box_from_extended_crop -- unexpected buffer_crop form.\n";
                    b.resize(mins_struct->args.size());
                    b.used = const_true();
                    for (size_t i = 0; i < mins_struct->args.size(); i++) {
                        Interval min_interval = bounds_of_expr_in_scope(mins_struct->args[i], scope, func_bounds);
                        Interval max_interval = bounds_of_expr_in_scope(mins_struct->args[i] + extents_struct->args[i] - 1, scope, func_bounds);
                        b[i] = Interval(min_interval.min, max_interval.max);
                    }
                    return true;
                }
            } else if (call_expr->name == Call::buffer_set_bounds) {
                internal_assert(call_expr->args.size() == 4)
                    << "Call::buffer_set_bounds call with unexpected number of arguments.\n";
                const IntImm *dim = call_expr->args[1].as<IntImm>();
                if (dim != nullptr && box_from_extended_crop(call_expr->args[0], b)) {
                    internal_assert(dim->value >= 0 && dim->value < (int64_t)b.size())
                        << "box_from_extended_crop setting bounds for out of range dim.\n";
                    Interval min_interval = bounds_of_expr_in_scope(call_expr->args[2], scope, func_bounds);
                    Interval max_interval = bounds_of_expr_in_scope(call_expr->args[2] + call_expr->args[3] - 1, scope, func_bounds);
                    b[dim->value] = Interval(min_interval.min, max_interval.max);
                    return true;
                }
            }
        }
        return false;
    }

    void visit(const Call *op) override {
        if (consider_calls) {
            if (op->is_intrinsic(Call::if_then_else)) {
                assert(op->args.size() == 3);
                // We wrap 'then_case' and 'else_case' inside 'dummy' call since IfThenElse
                // only takes Stmts as arguments.
                Stmt then_case = Evaluate::make(op->args[1]);
                Stmt else_case = Evaluate::make(op->args[2]);
                Stmt equivalent_if = IfThenElse::make(op->args[0], then_case, else_case);
                equivalent_if.accept(this);
                return;
            }

            IRGraphVisitor::visit(op);

            if (op->call_type == Call::Halide ||
                op->call_type == Call::Image) {
                for (Expr e : op->args) {
                    e.accept(this);
                }
                if (op->name == func || func.empty()) {
                    Box b(op->args.size());
                    b.used = const_true();
                    for (size_t i = 0; i < op->args.size(); i++) {
                        b[i] = bounds_of_expr_in_scope(op->args[i], scope, func_bounds);
                    }
                    merge_boxes(boxes[op->name], b);
                }
            }
        }

        if (op->is_extern() && (in_producer || consider_calls)) {
            if (op->name == "halide_buffer_copy") {
                // Call doesn't yet have user_context inserted, so size is 3.
                internal_assert(op->args.size() == 3) << "Unexpected arg list size for halide_buffer_copy\n";
                for (int i = 0; i < 2; i++) {
                    // If considering calls, merge in the source bounds.
                    // If considering provides, merge in the destination bounds.
                    int var_index;
                    if (i == 0 && consider_calls) {
                        var_index = 0;
                    } else if (i == 1 && consider_provides && in_producer) {
                        var_index = 2;
                    } else {
                        continue;
                    }

                    const Variable *var = op->args[var_index].as<Variable>();
                    if (var != nullptr && var->type == type_of<halide_buffer_t *>()) {
                        if (func.empty() || starts_with(var->name, func)) {
                            const auto iter = buffer_lets.find(var->name);
                            if (iter != buffer_lets.end()) {
                                Box b;
                                if (box_from_extended_crop(iter->second, b)) {
                                    merge_boxes(boxes[func], b);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    class CountVars : public IRVisitor {
        using IRVisitor::visit;

        void visit(const Variable *var) override {
            count++;
        }

    public:
        int count;
        CountVars()
            : count(0) {
        }
    };

    // We get better simplification if we directly substitute mins
    // and maxes in, but this can also cause combinatorial code
    // explosion. Here we manage this by only substituting in
    // reasonably-sized expressions. We determine the size by
    // counting the number of var nodes.
    bool is_small_enough_to_substitute(Expr e) {
        CountVars c;
        e.accept(&c);
        return c.count < 10;
    }

    void push_var(const string &name) {
        auto iter = vars_renaming.find(name);
        if (iter == vars_renaming.end()) {
            vars_renaming.emplace(name, 0);
        } else {
            iter->second += 1;
        }
    }

    void pop_var(const string &name) {
        auto iter = vars_renaming.find(name);
        internal_assert(iter != vars_renaming.end());
        iter->second -= 1;
        if (iter->second < 0) {
            vars_renaming.erase(iter);
        }
    }

    VarInstance get_var_instance(const string &name) {
        // It is possible for the variable to be not in 'vars_renaming', e.g.
        // the output buffer min/max. In this case, we just add the variable
        // to the renaming map and assign it to instance 0.
        return VarInstance(name, vars_renaming[name]);
    }

    template<typename LetOrLetStmt>
    void visit_let(const LetOrLetStmt *op) {
        using is_let_stmt = typename std::is_same<LetOrLetStmt, LetStmt>;

        // LetStmts can be deeply stacked, and this visitor is called
        // before dead lets are eliminated, so we move all the
        // internal state off the call stack into an explicit stack on
        // the heap.
        struct Frame {
            set<string> old_let_vars;
            const LetOrLetStmt *op;
            VarInstance vi;
            CollectVars collect;
            string max_name, min_name;
            Interval value_bounds;
            Frame(const LetOrLetStmt *op)
                : op(op), collect(op->name) {
            }
        };

        vector<Frame> frames;
        decltype(op->body) result;
        while (op) {
            frames.emplace_back(op);
            Frame &f = frames.back();
            push_var(op->name);

            if (op->value.type() == type_of<struct halide_buffer_t *>()) {
                buffer_lets[op->name] = op->value;
            }

            if (is_let_stmt::value) {
                f.vi = get_var_instance(op->name);

                // Update the 'children' map.
                op->value.accept(&f.collect);
                for (const auto &v : f.collect.vars) {
                    children[get_var_instance(v)].insert(f.vi);
                }

                // If this let stmt is a redefinition of a previous one, we should
                // remove the old let stmt from the 'children' map since it is
                // no longer valid at this point.
                if ((f.vi.instance > 0) && let_stmts.contains(op->name)) {
                    const Expr &val = let_stmts.get(op->name);
                    CollectVars collect(op->name);
                    val.accept(&collect);
                    f.old_let_vars = collect.vars;

                    VarInstance old_vi = VarInstance(f.vi.var, f.vi.instance - 1);
                    for (const auto &v : f.old_let_vars) {
                        internal_assert(vars_renaming.count(v));
                        children[get_var_instance(v)].erase(old_vi);
                    }
                }
                let_stmts.push(op->name, op->value);
            }

            op->value.accept(this);

            f.value_bounds = bounds_of_expr_in_scope(op->value, scope, func_bounds);

            bool fixed = f.value_bounds.min.same_as(f.value_bounds.max);
            f.value_bounds.min = simplify(f.value_bounds.min);
            f.value_bounds.max = fixed ? f.value_bounds.min : simplify(f.value_bounds.max);

            if (is_small_enough_to_substitute(f.value_bounds.min) &&
                (fixed || is_small_enough_to_substitute(f.value_bounds.max))) {
                scope.push(op->name, f.value_bounds);
            } else {
                f.max_name = unique_name('t');
                f.min_name = unique_name('t');
                scope.push(op->name, Interval(Variable::make(op->value.type(), f.min_name),
                                              Variable::make(op->value.type(), f.max_name)));
            }

            result = op->body;
            op = result.template as<LetOrLetStmt>();
        }

        result.accept(this);

        for (auto it = frames.rbegin(); it != frames.rend(); it++) {
            // Pop the value bounds
            scope.pop(it->op->name);

            if (it->op->value.type() == type_of<struct halide_buffer_t *>()) {
                buffer_lets.erase(it->op->name);
            }

            if (!it->min_name.empty()) {
                // We made up new names for the bounds of the
                // value, and need to rewrap any boxes we're
                // returning with appropriate lets.
                for (pair<const string, Box> &i : boxes) {
                    Box &box = i.second;
                    for (size_t i = 0; i < box.size(); i++) {
                        if (box[i].has_lower_bound()) {
                            if (expr_uses_var(box[i].min, it->max_name)) {
                                box[i].min = Let::make(it->max_name, it->value_bounds.max, box[i].min);
                            }
                            if (expr_uses_var(box[i].min, it->min_name)) {
                                box[i].min = Let::make(it->min_name, it->value_bounds.min, box[i].min);
                            }
                        }
                        if (box[i].has_upper_bound()) {
                            if (expr_uses_var(box[i].max, it->max_name)) {
                                box[i].max = Let::make(it->max_name, it->value_bounds.max, box[i].max);
                            }
                            if (expr_uses_var(box[i].max, it->min_name)) {
                                box[i].max = Let::make(it->min_name, it->value_bounds.min, box[i].max);
                            }
                        }
                    }
                }
            }

            if (is_let_stmt::value) {
                let_stmts.pop(it->op->name);

                // If this let stmt shadowed an outer one, we need
                // to re-insert the children from the previous let
                // stmt into the map.
                if (!it->old_let_vars.empty()) {
                    internal_assert(it->vi.instance > 0);
                    VarInstance old_vi = VarInstance(it->vi.var, it->vi.instance - 1);
                    for (const auto &v : it->old_let_vars) {
                        internal_assert(vars_renaming.count(v));
                        children[get_var_instance(v)].insert(old_vi);
                    }
                }

                // Remove the children from the current let stmt.
                for (const auto &v : it->collect.vars) {
                    internal_assert(vars_renaming.count(v));
                    children[get_var_instance(v)].erase(it->vi);
                }
            }

            pop_var(it->op->name);
        }
    }

    void visit(const Let *op) override {
        visit_let(op);
    }

    void visit(const LetStmt *op) override {
        visit_let(op);
    }

    struct LetBound {
        string var, min_name, max_name;
        LetBound(const string &v, const string &min, const string &max)
            : var(v), min_name(min), max_name(max) {
        }
    };

    void trim_scope_push(const string &name, const Interval &bound, vector<LetBound> &let_bounds) {
        scope.push(name, bound);

        for (const auto &v : children[get_var_instance(name)]) {
            string max_name = unique_name('t');
            string min_name = unique_name('t');

            let_bounds.insert(let_bounds.begin(), LetBound(v.var, min_name, max_name));

            internal_assert(let_stmts.contains(v.var));
            Type t = let_stmts.get(v.var).type();
            Interval b = Interval(Variable::make(t, min_name), Variable::make(t, max_name));
            trim_scope_push(v.var, b, let_bounds);
        }
    }

    void trim_scope_pop(const string &name, vector<LetBound> &let_bounds) {
        while (!let_bounds.empty()) {
            LetBound l = let_bounds.back();
            let_bounds.pop_back();

            trim_scope_pop(l.var, let_bounds);

            for (pair<const string, Box> &i : boxes) {
                Box &box = i.second;
                for (size_t i = 0; i < box.size(); i++) {
                    Interval v_bound;
                    if ((box[i].has_lower_bound() && (expr_uses_var(box[i].min, l.max_name) ||
                                                      expr_uses_var(box[i].min, l.min_name))) ||
                        (box[i].has_upper_bound() && (expr_uses_var(box[i].max, l.max_name) ||
                                                      expr_uses_var(box[i].max, l.min_name)))) {
                        internal_assert(let_stmts.contains(l.var));
                        const Expr &val = let_stmts.get(l.var);
                        v_bound = bounds_of_expr_in_scope(val, scope, func_bounds);
                        bool fixed = v_bound.min.same_as(v_bound.max);
                        v_bound.min = simplify(v_bound.min);
                        v_bound.max = fixed ? v_bound.min : simplify(v_bound.max);

                        internal_assert(scope.contains(l.var));
                        const Interval &old_bound = scope.get(l.var);
                        v_bound.max = simplify(min(v_bound.max, old_bound.max));
                        v_bound.min = simplify(max(v_bound.min, old_bound.min));
                    }

                    if (box[i].has_lower_bound()) {
                        if (expr_uses_var(box[i].min, l.max_name)) {
                            box[i].min = Let::make(l.max_name, v_bound.max, box[i].min);
                        }
                        if (expr_uses_var(box[i].min, l.min_name)) {
                            box[i].min = Let::make(l.min_name, v_bound.min, box[i].min);
                        }
                    }
                    if (box[i].has_upper_bound()) {
                        if (expr_uses_var(box[i].max, l.max_name)) {
                            box[i].max = Let::make(l.max_name, v_bound.max, box[i].max);
                        }
                        if (expr_uses_var(box[i].max, l.min_name)) {
                            box[i].max = Let::make(l.min_name, v_bound.min, box[i].max);
                        }
                    }
                }
            }
        }
        scope.pop(name);
    }

    vector<const Variable *> find_free_vars(Expr e) {
        class FindFreeVars : public IRVisitor {
            using IRVisitor::visit;
            void visit(const Variable *op) override {
                if (scope.contains(op->name)) {
                    result.push_back(op);
                }
            }

        public:
            const Scope<Interval> &scope;
            vector<const Variable *> result;
            FindFreeVars(const Scope<Interval> &s)
                : scope(s) {
            }
        } finder(scope);
        e.accept(&finder);
        return finder.result;
    }

    void visit(const IfThenElse *op) override {
        op->condition.accept(this);
        if (expr_uses_vars(op->condition, scope)) {
            // We need to simplify the condition to get it into a
            // canonical form (e.g. (a < b) instead of !(a >= b))
            vector<pair<Expr, Stmt>> cases;
            {
                Expr c = simplify(op->condition);
                cases.emplace_back(c, op->then_case);
                if (op->else_case.defined() && !is_no_op(op->else_case)) {
                    cases.emplace_back(simplify(!c), op->else_case);
                }
            }
            for (const auto &pair : cases) {
                Expr c = pair.first;
                Stmt body = pair.second;
                const Call *call = c.as<Call>();
                if (call && (call->is_intrinsic(Call::likely) ||
                             call->is_intrinsic(Call::likely_if_innermost) ||
                             call->is_intrinsic(Call::strict_float))) {
                    c = call->args[0];
                }

                // Find the vars that vary, and solve for each in turn
                // in order to bound it using the RHS. Maintain a list
                // of the things we need to pop from scope once we're
                // done.
                struct RestrictedVar {
                    // This variable
                    const Variable *v;
                    // Takes on this range
                    Interval i;
                    // Implying that these other variables also have a restricted range
                    vector<LetBound> let_bounds;
                };
                vector<RestrictedVar> to_pop;
                auto vars = find_free_vars(op->condition);
                for (auto v : vars) {
                    auto result = solve_expression(c, v->name);
                    if (!result.fully_solved) continue;
                    Expr solved = result.result;

                    // Trim the scope down to represent the fact that the
                    // condition is true. We only understand certain types
                    // of conditions for now.

                    const LT *lt = solved.as<LT>();
                    const LE *le = solved.as<LE>();
                    const GT *gt = solved.as<GT>();
                    const GE *ge = solved.as<GE>();
                    const EQ *eq = solved.as<EQ>();
                    Expr lhs, rhs;
                    if (lt) {
                        lhs = lt->a;
                        rhs = lt->b;
                    } else if (le) {
                        lhs = le->a;
                        rhs = le->b;
                    } else if (gt) {
                        lhs = gt->a;
                        rhs = gt->b;
                    } else if (ge) {
                        lhs = ge->a;
                        rhs = ge->b;
                    } else if (eq) {
                        lhs = eq->a;
                        rhs = eq->b;
                    }

                    if (!rhs.defined() || rhs.type() != Int(32)) {
                        continue;
                    }

                    if (!equal(lhs, v)) {
                        continue;
                    }

                    Expr inner_min, inner_max;
                    Interval i = scope.get(v->name);

                    // If the original condition is likely, then
                    // the additional trimming of the domain due
                    // to the condition is probably unnecessary,
                    // which means the mins/maxes below should
                    // probably just be the LHS.
                    Interval likely_i = i;
                    if (call && call->is_intrinsic(Call::likely)) {
                        likely_i.min = likely(i.min);
                        likely_i.max = likely(i.max);
                    } else if (call && call->is_intrinsic(Call::likely_if_innermost)) {
                        likely_i.min = likely_if_innermost(i.min);
                        likely_i.max = likely_if_innermost(i.max);
                    }

                    Interval bi = bounds_of_expr_in_scope(rhs, scope, func_bounds);
                    if (bi.has_upper_bound() && i.has_upper_bound()) {
                        if (lt) {
                            i.max = min(likely_i.max, bi.max - 1);
                        }
                        if (le || eq) {
                            i.max = min(likely_i.max, bi.max);
                        }
                    }
                    if (bi.has_lower_bound() && i.has_lower_bound()) {
                        if (gt) {
                            i.min = max(likely_i.min, bi.min + 1);
                        }
                        if (ge || eq) {
                            i.min = max(likely_i.min, bi.min);
                        }
                    }
                    RestrictedVar p;
                    p.v = v;
                    p.i = i;
                    to_pop.emplace_back(std::move(p));
                }
                for (auto &p : to_pop) {
                    trim_scope_push(p.v->name, p.i, p.let_bounds);
                }
                body.accept(this);
                while (!to_pop.empty()) {
                    trim_scope_pop(to_pop.back().v->name, to_pop.back().let_bounds);
                    to_pop.pop_back();
                }
            }
        } else {
            // If the condition is based purely on params, then we'll only
            // ever go one way in a given run, so we should conditionalize
            // the boxes touched on the condition.

            // Fork the boxes touched and go down each path
            map<string, Box> then_boxes, else_boxes;
            then_boxes.swap(boxes);
            op->then_case.accept(this);
            then_boxes.swap(boxes);

            if (op->else_case.defined()) {
                else_boxes.swap(boxes);
                op->else_case.accept(this);
                else_boxes.swap(boxes);
            }

            // Make sure all the then boxes have an entry on the else
            // side so that the merge doesn't skip them.
            for (pair<const string, Box> &i : then_boxes) {
                else_boxes[i.first];
            }

            // Merge
            for (pair<const string, Box> &i : else_boxes) {
                Box &else_box = i.second;
                Box &then_box = then_boxes[i.first];
                Box &orig_box = boxes[i.first];

                if (then_box.maybe_unused()) {
                    then_box.used = then_box.used && op->condition;
                } else {
                    then_box.used = op->condition;
                }

                if (else_box.maybe_unused()) {
                    else_box.used = else_box.used && !op->condition;
                } else {
                    else_box.used = !op->condition;
                }

                merge_boxes(then_box, else_box);
                merge_boxes(orig_box, then_box);
            }
        }
    }

    void visit(const For *op) override {
        if (consider_calls) {
            op->min.accept(this);
            op->extent.accept(this);
        }

        Expr min_val, max_val;
        if (scope.contains(op->name + ".loop_min")) {
            min_val = scope.get(op->name + ".loop_min").min;
        } else {
            min_val = bounds_of_expr_in_scope(op->min, scope, func_bounds).min;
        }

        if (scope.contains(op->name + ".loop_max")) {
            max_val = scope.get(op->name + ".loop_max").max;
        } else {
            max_val = bounds_of_expr_in_scope(op->extent, scope, func_bounds).max;
            max_val += bounds_of_expr_in_scope(op->min, scope, func_bounds).max;
            max_val -= 1;
        }

        push_var(op->name);
        {
            ScopedBinding<Interval> p(scope, op->name, Interval(min_val, max_val));
            op->body.accept(this);
        }
        pop_var(op->name);
    }

    void visit(const Provide *op) override {
        if (consider_provides) {
            if (op->name == func || func.empty()) {
                Box b(op->args.size());
                for (size_t i = 0; i < op->args.size(); i++) {
                    b[i] = bounds_of_expr_in_scope(op->args[i], scope, func_bounds);
                }
                merge_boxes(boxes[op->name], b);
            }
        }

        if (consider_calls) {
            for (size_t i = 0; i < op->args.size(); i++) {
                op->args[i].accept(this);
            }
            for (size_t i = 0; i < op->values.size(); i++) {
                op->values[i].accept(this);
            }
        }
    }

    void visit(const ProducerConsumer *op) override {
        if (op->is_producer && (op->name == func || func.empty())) {
            ScopedValue<bool> save_in_producer(in_producer, true);
            IRGraphVisitor::visit(op);
        } else {
            IRGraphVisitor::visit(op);
        }
    }
};

map<string, Box> boxes_touched(Expr e, Stmt s, bool consider_calls, bool consider_provides,
                               string fn, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    if (!fn.empty() && s.defined()) {
        // Filter things down to the relevant sub-Stmts, so we don't spend a
        // long time reasoning about lets and ifs that don't surround an
        // access to the buffer in question.

        class Filter : public IRMutator {
            using IRMutator::mutate;
            using IRMutator::visit;

            bool relevant = false;

            Expr visit(const Call *op) override {
                if (op->name == fn) {
                    relevant = true;
                    return op;
                } else {
                    return IRMutator::visit(op);
                }
            }

            Stmt visit(const Provide *op) override {
                if (op->name == fn) {
                    relevant = true;
                    return op;
                } else {
                    return IRMutator::visit(op);
                }
            }

            Expr visit(const Variable *op) override {
                if (op->name == fn_buffer) {
                    relevant = true;
                }
                return op;
            }

        public:
            Stmt mutate(const Stmt &s) override {
                bool old = relevant;
                relevant = false;
                Stmt s_new = IRMutator::mutate(s);
                if (!relevant) {
                    relevant = old;
                    return no_op;
                } else {
                    return s_new;
                }
            }

            const string &fn;
            const string fn_buffer;
            Stmt no_op;
            Filter(const string &fn)
                : fn(fn), fn_buffer(fn + ".buffer"), no_op(Evaluate::make(0)) {
            }
        } filter(fn);

        s = filter.mutate(s);
    }

    // Move the innermost vars in an IfThenElse's condition as far to the left
    // as possible, so that BoxesTouched can prune the variable scope tighter
    // when encountering the IfThenElse.
    if (s.defined()) {
        s = SolveIfThenElse().mutate(s);
    }

    // Do calls and provides separately, for better simplification.
    BoxesTouched calls(consider_calls, false, fn, &scope, fb);
    BoxesTouched provides(false, consider_provides, fn, &scope, fb);

    if (consider_calls) {
        if (e.defined()) {
            e.accept(&calls);
        }
        if (s.defined()) {
            s.accept(&calls);
        }
    }
    if (consider_provides) {
        if (e.defined()) {
            e.accept(&provides);
        }
        if (s.defined()) {
            s.accept(&provides);
        }
    }
    if (!consider_calls) {
        return provides.boxes;
    }
    if (!consider_provides) {
        return calls.boxes;
    }

    // Combine the two maps.
    for (pair<const string, Box> &i : provides.boxes) {
        merge_boxes(calls.boxes[i.first], i.second);
    }

    // Make evaluating these boxes side-effect-free
    for (auto &p : calls.boxes) {
        auto &box = p.second;
        box.used = purify_index_math(box.used);
        for (Interval &i : box.bounds) {
            i.min = purify_index_math(i.min);
            i.max = purify_index_math(i.max);
        }
    }

    return calls.boxes;
}

Box box_touched(Expr e, Stmt s, bool consider_calls, bool consider_provides,
                string fn, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    map<string, Box> boxes = boxes_touched(e, s, consider_calls, consider_provides, fn, scope, fb);
    internal_assert(boxes.size() <= 1);
    return boxes[fn];
}

map<string, Box> boxes_required(Expr e, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    return boxes_touched(e, Stmt(), true, false, "", scope, fb);
}

Box box_required(Expr e, string fn, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    return box_touched(e, Stmt(), true, false, fn, scope, fb);
}

map<string, Box> boxes_required(Stmt s, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    return boxes_touched(Expr(), s, true, false, "", scope, fb);
}

Box box_required(Stmt s, string fn, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    return box_touched(Expr(), s, true, false, fn, scope, fb);
}

map<string, Box> boxes_provided(Expr e, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    return boxes_touched(e, Stmt(), false, true, "", scope, fb);
}

Box box_provided(Expr e, string fn, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    return box_touched(e, Stmt(), false, true, fn, scope, fb);
}

map<string, Box> boxes_provided(Stmt s, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    return boxes_touched(Expr(), s, false, true, "", scope, fb);
}

Box box_provided(Stmt s, string fn, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    return box_touched(Expr(), s, false, true, fn, scope, fb);
}

map<string, Box> boxes_touched(Expr e, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    return boxes_touched(e, Stmt(), true, true, "", scope, fb);
}

Box box_touched(Expr e, string fn, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    return box_touched(e, Stmt(), true, true, fn, scope, fb);
}

map<string, Box> boxes_touched(Stmt s, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    return boxes_touched(Expr(), s, true, true, "", scope, fb);
}

Box box_touched(Stmt s, string fn, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    return box_touched(Expr(), s, true, true, fn, scope, fb);
}

// Compute interval of all possible function's values (default + specialized values)
Interval compute_pure_function_definition_value_bounds(
    const Definition &def, const Scope<Interval> &scope, const FuncValueBounds &fb, int dim) {

    Interval result = bounds_of_expr_in_scope(def.values()[dim], scope, fb);

    // Pure function might have different values due to specialization.
    // We need to take the union of min and max bounds of all those possible values.
    for (const Specialization &s : def.specializations()) {
        Interval s_interval = compute_pure_function_definition_value_bounds(s.definition, scope, fb, dim);
        result.include(s_interval);
    }
    return result;
}

FuncValueBounds compute_function_value_bounds(const vector<string> &order,
                                              const map<string, Function> &env) {
    FuncValueBounds fb;

    for (size_t i = 0; i < order.size(); i++) {
        Function f = env.find(order[i])->second;
        const vector<string> f_args = f.args();
        for (int j = 0; j < f.outputs(); j++) {
            pair<string, int> key = {f.name(), j};

            Interval result;

            if (f.is_pure()) {

                // Make a scope that says the args could be anything.
                Scope<Interval> arg_scope;
                for (size_t k = 0; k < f.args().size(); k++) {
                    arg_scope.push(f_args[k], Interval::everything());
                }

                result = compute_pure_function_definition_value_bounds(f.definition(), arg_scope, fb, j);
                // These can expand combinatorially as we go down the
                // pipeline if we don't run CSE on them.
                if (result.has_lower_bound()) {
                    result.min = simplify(common_subexpression_elimination(result.min));
                }

                if (result.has_upper_bound()) {
                    result.max = simplify(common_subexpression_elimination(result.max));
                }

                fb[key] = result;
            } else {
                // If the Func is impure, we may still be able to specify a bounds-of-type here
                Type t = f.output_types()[j].element_of();
                if ((t.is_uint() || t.is_int()) && t.bits() <= 16) {
                    result = Interval(t.min(), t.max());
                } else {
                    result = Interval::everything();
                }
                fb[key] = result;

                // TODO: if a Function is impure, but the RDoms used by the update functions
                // are all constant, it may be profitable to calculate the bounds here too
            }

            debug(2) << "Bounds on value " << j
                     << " for func " << order[i]
                     << " are: " << result.min << ", " << result.max << "\n";
        }
    }

    return fb;
}

namespace {

void check(const Scope<Interval> &scope, Expr e, Expr correct_min, Expr correct_max) {
    FuncValueBounds fb;
    Interval result = bounds_of_expr_in_scope(e, scope, fb);
    result.min = simplify(result.min);
    result.max = simplify(result.max);
    if (!equal(result.min, correct_min)) {
        internal_error << "In bounds of " << e << ":\n"
                       << "Incorrect min: " << result.min << '\n'
                       << "Should have been: " << correct_min << '\n';
    }
    if (!equal(result.max, correct_max)) {
        internal_error << "In bounds of " << e << ":\n"
                       << "Incorrect max: " << result.max << '\n'
                       << "Should have been: " << correct_max << '\n';
    }
}

void check_constant_bound(const Scope<Interval> &scope, Expr e, Expr correct_min, Expr correct_max) {
    FuncValueBounds fb;
    Interval result = bounds_of_expr_in_scope(e, scope, fb, true);
    result.min = simplify(result.min);
    result.max = simplify(result.max);
    if (!equal(result.min, correct_min)) {
        internal_error << "In find constant bound of " << e << ":\n"
                       << "Incorrect min constant bound: " << result.min << '\n'
                       << "Should have been: " << correct_min << '\n';
    }
    if (!equal(result.max, correct_max)) {
        internal_error << "In find constant bound of " << e << ":\n"
                       << "Incorrect max constant bound: " << result.max << '\n'
                       << "Should have been: " << correct_max << '\n';
    }
}

void check_constant_bound(Expr e, Expr correct_min, Expr correct_max) {
    Scope<Interval> scope;
    check_constant_bound(scope, e, correct_min, correct_max);
}

void constant_bound_test() {
    using namespace ConciseCasts;

    {
        Param<int16_t> a;
        Param<uint16_t> b;
        check_constant_bound(a >> b, i16(-32768), i16(32767));
    }

    {
        Param<int> x("x"), y("y");
        x.set_range(10, 20);
        y.set_range(5, 30);
        check_constant_bound(clamp(x, 5, 30), 10, 20);
        check_constant_bound(clamp(x, 15, 30), 15, 20);
        check_constant_bound(clamp(x, 15, 17), 15, 17);
        check_constant_bound(clamp(x, 5, 15), 10, 15);

        check_constant_bound(x + y, 15, 50);
        check_constant_bound(x - y, -20, 15);
        check_constant_bound(x * y, 50, 600);
        check_constant_bound(x / y, 0, 4);

        check_constant_bound(select(x > 4, 3 * x - y / 2, max(x + y + 2, x - 20)), 15, 58);
        check_constant_bound(select(x < 4, 3 * x - y / 2, max(x + y + 2, x - 20)), 17, 52);
        check_constant_bound(select(x >= 11, 3 * x - y / 2, max(x + y + 2, x - 20)), 15, 58);
    }

    {
        Param<uint8_t> x("x"), y("y");
        x.set_range(Expr((uint8_t)10), Expr((uint8_t)20));
        y.set_range(Expr((uint8_t)5), Expr((uint8_t)30));
        check_constant_bound(clamp(x, 5, 30), Expr((uint8_t)10), Expr((uint8_t)20));
        check_constant_bound(clamp(x, 15, 30), Expr((uint8_t)15), Expr((uint8_t)20));
        check_constant_bound(clamp(x, 15, 17), Expr((uint8_t)15), Expr((uint8_t)17));
        check_constant_bound(clamp(x, 5, 15), Expr((uint8_t)10), Expr((uint8_t)15));

        check_constant_bound(x + y, Expr((uint8_t)15), Expr((uint8_t)50));
        check_constant_bound(x / y, Expr((uint8_t)0), Expr((uint8_t)4));

        check_constant_bound(select(x > 4, 3 * x - y / 2, max(x + y + 2, x + 20)),
                             Expr((uint8_t)15), Expr((uint8_t)58));
        check_constant_bound(select(x < 4, 3 * x - y / 2, max(x + y + 2, x + 20)),
                             Expr((uint8_t)30), Expr((uint8_t)52));
        check_constant_bound(select(x >= 11, 3 * x - y / 2, max(x + y + 2, x + 20)),
                             Expr((uint8_t)15), Expr((uint8_t)58));

        // These two overflow
        check_constant_bound(x - y, Expr((uint8_t)0), Expr((uint8_t)255));
        check_constant_bound(x * y, Expr((uint8_t)0), Expr((uint8_t)255));

        check_constant_bound(absd(x, y), Expr((uint8_t)0), Expr((uint8_t)20));
        check_constant_bound(absd(cast<int16_t>(x), cast<int16_t>(y)), Expr((uint16_t)0), Expr((uint16_t)20));
    }

    {
        Param<float> x("x"), y("y");
        x.set_range(Expr((float)10), Expr((float)20));
        y.set_range(Expr((float)5), Expr((float)30));

        check_constant_bound(absd(x, y), Expr((float)0), Expr((float)20));
    }

    {
        Param<int8_t> i("i"), x("x"), y("y"), d("d");
        Expr cl = i16(i);
        Expr cr1 = i16(x);
        Expr cr2 = i16(y);
        Expr fraction = (d & (int16_t)((1 << 7) - 1));
        Expr cr = i16((((cr2 - cr1) * fraction) >> 7) + cr1);

        check_constant_bound(absd(cr, cl), Expr((uint16_t)0), Expr((uint16_t)509));
        check_constant_bound(i16(absd(cr, cl)), Expr((int16_t)0), Expr((int16_t)509));
    }

    check_constant_bound(Load::make(Int(32), "buf", 0, Buffer<>(), Parameter(), const_true(), ModulusRemainder()) * 20,
                         Interval::neg_inf(), Interval::pos_inf());

    {
        // Ensure that unnecessary integer overflow doesn't happen
        // in cases involving unsigned integer math
        Param<uint16_t> e1("e1");      // range 0..0xffff, type=uint16
        Expr e2 = cast<uint32_t>(e1);  // range 0..0xffff, type=uint32
        Expr e3 = e2 * e2;             // range 0..0xfffe0001, type=uint32
        check_constant_bound(e3, Expr((uint32_t)0), Expr((uint32_t)0xfffe0001));
    }

    {
        RDom r(0, 4);

        // bounds of an expression with impure >= 32 bit expr will be unbounded
        Expr e32 = sum(cast<int32_t>(r.x));
        check_constant_bound(e32, Interval::neg_inf(), Interval::pos_inf());

        // bounds of an expression with impure < 32 bit expr will be bounds-of-type
        Expr e16 = sum(cast<int16_t>(r.x));
        check_constant_bound(e16, Int(16).min(), Int(16).max());
    }

    {
        Param<int32_t> x("x"), y("y");
        x.set_range(2, 10);

        check_constant_bound(count_leading_zeros(x), i32(28), i32(30));
        check_constant_bound(count_leading_zeros(cast<int16_t>(x)), i16(12), i16(14));

        check_constant_bound(count_leading_zeros(y), i32(0), i32(32));
        check_constant_bound(count_leading_zeros(cast<int16_t>(y)), i16(0), i16(16));
    }
}

void boxes_touched_test() {
    Type t = Int(32);
    Expr x = Variable::make(t, "x");
    Expr y = Variable::make(t, "y");
    Expr z = Variable::make(t, "z");
    Expr w = Variable::make(t, "w");

    Scope<Interval> scope;
    scope.push("y", Interval(Expr(0), Expr(10)));

    Stmt stmt = Provide::make("f", {10}, {x, y, z, w});
    stmt = IfThenElse::make(y > 4, stmt, Stmt());
    stmt = IfThenElse::make(z > 18, stmt, Stmt());
    stmt = LetStmt::make("w", z + 3, stmt);
    stmt = LetStmt::make("z", x + 2, stmt);
    stmt = LetStmt::make("x", y + 10, stmt);

    Box expected({Interval(15, 20), Interval(5, 10), Interval(19, 22), Interval(22, 25)});
    Box result = box_provided(stmt, "f", scope);
    internal_assert(expected.size() == result.size())
        << "Expect dim size of " << expected.size()
        << ", got " << result.size() << " instead\n";
    for (size_t i = 0; i < result.size(); ++i) {
        const Interval &correct = expected[i];
        Interval b = result[i];
        b.min = simplify(b.min);
        b.max = simplify(b.max);
        if (!equal(correct.min, b.min)) {
            internal_error << "In bounds of dim " << i << ":\n"
                           << "Incorrect min: " << b.min << '\n'
                           << "Should have been: " << correct.min << '\n';
        }
        if (!equal(correct.max, b.max)) {
            internal_error << "In bounds of dim " << i << ":\n"
                           << "Incorrect max: " << b.max << '\n'
                           << "Should have been: " << correct.max << '\n';
        }
    }
}

}  // anonymous namespace

void bounds_test() {
    using namespace Halide::ConciseCasts;

    constant_bound_test();

    Scope<Interval> scope;
    Var x("x"), y("y");
    scope.push("x", Interval(Expr(0), Expr(10)));

    check(scope, x, 0, 10);
    check(scope, x + 1, 1, 11);
    check(scope, (x + 1) * 2, 2, 22);
    check(scope, x * x, 0, 100);
    check(scope, 5 - x, -5, 5);
    check(scope, x * (5 - x), -50, 50);  // We don't expect bounds analysis to understand correlated terms
    check(scope, Select::make(x < 4, x, x + 100), 0, 110);
    check(scope, x + y, y, y + 10);
    check(scope, x * y, min(y, 0) * 10, max(y, 0) * 10);
    check(scope, x / (x + y), Interval::neg_inf(), Interval::pos_inf());
    check(scope, 11 / (x + 1), 1, 11);
    check(scope, Load::make(Int(8), "buf", x, Buffer<>(), Parameter(), const_true(), ModulusRemainder()),
          i8(-128), i8(127));
    check(scope, y + (Let::make("y", x + 3, y - x + 10)), y + 3, y + 23);  // Once again, we don't know that y is correlated with x
    check(scope, clamp(1 / (x - 2), x - 10, x + 10), -10, 20);
    check(scope, cast<uint16_t>(x / 2), u16(0), u16(5));
    check(scope, cast<uint16_t>((x + 10) / 2), u16(5), u16(10));
    check(scope, x < 20, make_bool(1), make_bool(1));
    check(scope, x < 5, make_bool(0), make_bool(1));
    check(scope, Broadcast::make(x >= 11, 3), make_bool(0), make_bool(0));
    check(scope, Ramp::make(x + 5, 1, 5) > Broadcast::make(2, 5), make_bool(1), make_bool(1));

    check(scope, print(x, y), 0, 10);
    check(scope, print_when(x > y, x, y), 0, 10);

    check(scope, select(y == 5, 0, 3), select(y == 5, 0, 3), select(y == 5, 0, 3));
    check(scope, select(y == 5, x, -3 * x + 8), select(y == 5, 0, -22), select(y == 5, 10, 8));
    check(scope, select(y == x, x, -3 * x + 8), -22, select(y <= 10 && 0 <= y, 10, 8));

    check(scope, cast<int32_t>(abs(cast<int16_t>(x ^ y))), 0, 32768);
    check(scope, cast<float>(x), 0.0f, 10.0f);

    check(scope, cast<int32_t>(abs(cast<float>(x))), 0, 10);

    // Check some vectors
    check(scope, Ramp::make(x * 2, 5, 5), 0, 40);
    check(scope, Broadcast::make(x * 2, 5), 0, 20);
    check(scope, Broadcast::make(3, 4), 3, 3);

    // Check some operations that may overflow
    check(scope, (cast<uint8_t>(x) + 250), u8(0), u8(255));
    check(scope, (cast<uint8_t>(x) + 10) * 20, u8(0), u8(255));
    check(scope, (cast<uint8_t>(x) + 10) * (cast<uint8_t>(x) + 5), u8(0), u8(255));
    check(scope, (cast<uint8_t>(x) + 10) - (cast<uint8_t>(x) + 5), u8(0), u8(255));

    // Check some operations that we should be able to prove do not overflow
    check(scope, (cast<uint8_t>(x) + 240), u8(240), u8(250));
    check(scope, (cast<uint8_t>(x) + 10) * 10, u8(100), u8(200));
    check(scope, (cast<uint8_t>(x) + 10) * (cast<uint8_t>(x)), u8(0), u8(200));
    check(scope, (cast<uint8_t>(x) + 20) - (cast<uint8_t>(x) + 5), u8(5), u8(25));

    // Check div/mod by unbounded unknowns. div and mod can only ever
    // make things smaller in magnitude.
    scope.push("x", Interval::everything());
    check(scope, -3 / x, -3, 3);
    check(scope, 3 / x, -3, 3);
    check(scope, y / x, -cast<int>(abs(y)), cast<int>(abs(y)));
    check(scope, -3 % x, 0, Interval::pos_inf());
    check(scope, 3 % x, 0, 3);
    // Mod can't make values negative
    check(scope, y % x, 0, Interval::pos_inf());
    // Mod can't make positive values larger
    check(scope, max(y, 0) % x, 0, max(y, 0));
    scope.pop("x");

    // Check some bitwise ops.
    check(scope, (cast<uint8_t>(x) & cast<uint8_t>(7)), u8(0), u8(7));
    check(scope, (cast<uint8_t>(3) & cast<uint8_t>(2)), u8(2), u8(2));
    check(scope, (cast<uint8_t>(1) | cast<uint8_t>(2)), u8(3), u8(3));
    check(scope, (cast<uint8_t>(3) ^ cast<uint8_t>(2)), u8(1), u8(1));
    check(scope, (~cast<uint8_t>(3)), u8(0xfc), u8(0xfc));
    check(scope, cast<uint8_t>(x + 5) & cast<uint8_t>(x + 3), u8(0), u8(13));
    check(scope, cast<int8_t>(x - 5) & cast<int8_t>(x + 3), i8(0), i8(13));
    check(scope, cast<int8_t>(2 * x - 5) & cast<int8_t>(x - 3), i8(-128), i8(15));
    check(scope, cast<uint8_t>(x + 5) | cast<uint8_t>(x + 3), u8(5), u8(255));
    check(scope, cast<int8_t>(x + 5) | cast<int8_t>(x + 3), i8(3), i8(127));
    check(scope, ~cast<uint8_t>(x), u8(-11), u8(-1));
    check(scope, (cast<uint8_t>(x) >> cast<uint8_t>(1)), u8(0), u8(5));
    check(scope, (cast<uint8_t>(10) >> cast<uint8_t>(1)), u8(5), u8(5));
    check(scope, (cast<uint8_t>(x + 3) << cast<uint8_t>(1)), u8(6), u8(26));
    check(scope, (cast<uint8_t>(x + 3) << cast<uint8_t>(7)), u8(0), u8(255));  // Overflows
    check(scope, (cast<uint8_t>(5) << cast<uint8_t>(1)), u8(10), u8(10));
    check(scope, (x << 12), 0, 10 << 12);
    check(scope, x & 4095, 0, 10);          // LHS known to be positive
    check(scope, x & 123, 0, 10);           // Doesn't have to be a precise bitmask
    check(scope, (x - 1) & 4095, 0, 4095);  // LHS could be -1

    // If we clamp something unbounded as one type, the bounds should
    // propagate through casts whenever the cast can be proved to not
    // overflow.
    check(scope,
          cast<uint16_t>(clamp(cast<float>(x ^ y), 0.0f, 4095.0f)),
          u16(0), u16(4095));

    check(scope,
          cast<uint8_t>(clamp(cast<uint16_t>(x ^ y), cast<uint16_t>(0), cast<uint16_t>(128))),
          u8(0), u8(128));

    Expr u8_1 = cast<uint8_t>(Load::make(Int(8), "buf", x, Buffer<>(), Parameter(), const_true(), ModulusRemainder()));
    Expr u8_2 = cast<uint8_t>(Load::make(Int(8), "buf", x + 17, Buffer<>(), Parameter(), const_true(), ModulusRemainder()));
    check(scope, cast<uint16_t>(u8_1) + cast<uint16_t>(u8_2),
          u16(0), u16(255 * 2));

    {
        Scope<Interval> scope;
        Expr x = Variable::make(UInt(16), "x");
        Expr y = Variable::make(UInt(16), "y");
        scope.push("x", Interval(u16(0), u16(10)));
        scope.push("y", Interval(u16(2), u16(4)));

        Expr e = clamp(x / y, u16(0), u16(128));
        check(scope, e, u16(0), u16(5));
        check_constant_bound(scope, e, u16(0), u16(5));
    }

    {
        Param<int16_t> x("x");
        Param<uint16_t> y("y");
        x.set_range(i16(-32), i16(-16));
        y.set_range(i16(0), i16(4));
        check_constant_bound((x >> y), i16(-32), i16(-1));
    }

    {
        Param<uint16_t> x("x"), y("y");
        x.set_range(u16(10), u16(20));
        y.set_range(u16(0), u16(30));
        Scope<Interval> scope;
        scope.push("y", Interval(u16(2), u16(4)));

        check_constant_bound(scope, x + y, u16(12), u16(24));
    }

    {
        Scope<Interval> scope;
        Interval i = Interval::everything();
        i.min = 17;
        internal_assert(i.has_lower_bound());
        internal_assert(!i.has_upper_bound());
        scope.push("y", i);
        Var x("x"), y("y");
        check(scope, select(x == y * 2, y, y - 10),
              7, Interval::pos_inf());
        check(scope, select(x == y * 2, y - 10, y),
              7, Interval::pos_inf());
    }

    vector<Expr> input_site_1 = {2 * x};
    vector<Expr> input_site_2 = {2 * x + 1};
    vector<Expr> output_site = {x + 1};

    Buffer<int32_t> in(10);
    in.set_name("input");

    Stmt loop = For::make("x", 3, 10, ForType::Serial, DeviceAPI::Host,
                          Provide::make("output",
                                        {Add::make(Call::make(in, input_site_1),
                                                   Call::make(in, input_site_2))},
                                        output_site));

    map<string, Box> r;
    r = boxes_required(loop);
    internal_assert(r.find("output") == r.end());
    internal_assert(r.find("input") != r.end());
    internal_assert(equal(simplify(r["input"][0].min), 6));
    internal_assert(equal(simplify(r["input"][0].max), 25));
    r = boxes_provided(loop);
    internal_assert(r.find("output") != r.end());
    internal_assert(equal(simplify(r["output"][0].min), 4));
    internal_assert(equal(simplify(r["output"][0].max), 13));

    Box r2({Interval(Expr(5), Expr(19))});
    merge_boxes(r2, r["output"]);
    internal_assert(equal(simplify(r2[0].min), 4));
    internal_assert(equal(simplify(r2[0].max), 19));

    boxes_touched_test();

    // Check a deeply-nested bitwise expr to ensure it doesn't take n^2 time
    // (this clause took ~30s on a typical laptop before the fix, ~10ms after)
    {
        Expr a = Variable::make(UInt(16), "t42");
        Expr b = Variable::make(UInt(16), "t43");
        Expr c = Variable::make(UInt(16), "t44");
        Expr d = Variable::make(Int(32), "d");
        Expr x = Variable::make(Int(32), "x");
        Expr y = Variable::make(Int(32), "y");
        Expr e1 = select(c >= Expr((uint16_t)128), c - Expr((uint16_t)128), c);
        Expr e2 = Let::make("t44", (((((((((((((((((u16(0) << u16(1)) | u16((u8(d) & u8(1)))) << u16(1)) | u16(((u8(d) >> u8(1)) & u8(1)))) << u16(1)) | (u16(x) & u16(1))) << u16(1)) | (u16(y) & u16(1))) << u16(1)) | (a & u16(1))) << u16(1)) | (b & u16(1))) << u16(1)) | ((a >> u16(1)) & u16(1))) << u16(1)) | ((b >> u16(1)) & u16(1))) >> u16(1)), e1);
        Expr e3 = Let::make("t43", u16(y) >> u16(1), e2);
        Expr e4 = Let::make("t42", u16(x) >> u16(1), e3);

        check_constant_bound(e4, u16(0), u16(65535));
    }

    std::cout << "Bounds test passed" << std::endl;
}

}  // namespace Internal
}  // namespace Halide

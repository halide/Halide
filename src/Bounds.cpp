#include <iostream>

#include "Bounds.h"
#include "IRVisitor.h"
#include "IR.h"
#include "IROperator.h"
#include "IREquality.h"
#include "Simplify.h"
#include "IRPrinter.h"
#include "Util.h"
#include "Var.h"
#include "Debug.h"
#include "ExprUsesVar.h"

namespace Halide {
namespace Internal {

using std::make_pair;
using std::map;
using std::vector;
using std::string;
using std::pair;

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
}

class Bounds : public IRVisitor {
public:
    Expr min, max;
    Scope<Interval> scope;
    const FuncValueBounds &func_bounds;

    Bounds(const Scope<Interval> *s, const FuncValueBounds &fb) :
        func_bounds(fb) {
        scope.set_containing_scope(s);
    }
private:

    // Compute the intrinsic bounds of a function.
    void bounds_of_func(Function f, int value_index) {
        // if we can't get a good bound from the function, fall back to the bounds of the type.
        bounds_of_type(f.output_types()[value_index]);

        pair<string, int> key = make_pair(f.name(), value_index);

        FuncValueBounds::const_iterator iter = func_bounds.find(key);

        if (iter != func_bounds.end()) {
            if (iter->second.min.defined()) {
                min = iter->second.min;
            }
            if (iter->second.max.defined()) {
                max = iter->second.max;
            }
        }
    }

    void bounds_of_type(Type t) {
        if (t.is_uint() && t.bits <= 16) {
            max = cast(t, (1 << t.bits) - 1);
            min = cast(t, 0);
        } else if (t.is_int() && t.bits <= 16) {
            max = cast(t, (1 << (t.bits-1)) - 1);
            min = cast(t, -(1 << (t.bits-1)));
        } else {
            max = Expr();
            min = Expr();
        }
    }

    using IRVisitor::visit;

    void visit(const IntImm *op) {
        min = op;
        max = op;
    }

    void visit(const FloatImm *op) {
        min = op;
        max = op;
    }

    void visit(const Cast *op) {

        op->value.accept(this);
        Expr min_a = min, max_a = max;

        if (min_a.same_as(op->value) && max_a.same_as(op->value)) {
            min = max = op;
            return;
        }

        Type to = op->type;
        Type from = op->value.type();

        if (min_a.defined() && min_a.same_as(max_a)) {
            min = max = Cast::make(to, min_a);
            return;
        }

        // If overflow is impossible, cast the min and max. If it's
        // possible, use the bounds of the destination type.
        bool could_overflow = true;
        if (to.is_float()) {
            could_overflow = false;
        } else if (to.is_int() && from.is_int() && to.bits >= from.bits) {
            could_overflow = false;
        } else if (to.is_uint() && from.is_uint() && to.bits >= from.bits) {
            could_overflow = false;
        } else if (to.is_int() && from.is_uint() && to.bits > from.bits) {
            could_overflow = false;
        } else if (to.is_int() && to.bits >= 32) {
            // Warning: dubious code ahead.

            // If we cast to an int32 or greater, assume that it won't
            // overflow. Otherwise expressions like
            // cast<int32_t>(bounded_float) barf.
            could_overflow = false;
        }

        // If min and max are different constants that fit into the
        // narrower type, we should allow it.
        if (from == Int(32) && min_a.defined() && max_a.defined()) {
            if (const IntImm *min_int = min_a.as<IntImm>()) {
                if (const IntImm *max_int = max_a.as<IntImm>()) {
                    if (to.is_uint() && to.bits <= 32 &&
                        min_int->value >= 0 &&
                        (to.bits == 32 || (max_int->value < (1 << to.bits)))) {
                        could_overflow = false;
                    } else if (to.is_int() && to.bits <= 32 &&
                               min_int->value >= -(1 << (to.bits-1)) &&
                               max_int->value < (1 << (to.bits-1))) {
                        could_overflow = false;
                    }
                }
            }
        }

        if (from == Float(32) && min_a.defined() && max_a.defined()) {
            if (const FloatImm *min_float = min_a.as<FloatImm>()) {
                if (const FloatImm *max_float = max_a.as<FloatImm>()) {
                    double max_magnitude = ::pow(2.0, to.bits-1);
                    if (to.is_uint() &&
                        min_float->value >= 0.0f &&
                        max_float->value < 2.0*max_magnitude) {
                        could_overflow = false;
                    } else if (to.is_int() &&
                               min_float->value >= -max_magnitude &&
                               max_float->value < max_magnitude) {
                        could_overflow = false;
                    }
                }
            }
        }

        if (!could_overflow) {
            // Start with the bounds of the narrow type.
            bounds_of_type(from);
            // If we have a better min or max for the arg use that.
            if (min_a.defined()) min = min_a;
            if (max_a.defined()) max = max_a;
            // Then cast those bounds to the wider type.
            if (min.defined()) min = Cast::make(to, min);
            if (max.defined()) max = Cast::make(to, max);
        } else {
            // This might overflow, so use the bounds of the destination type.
            bounds_of_type(to);
        }
    }

    void visit(const Variable *op) {
        if (scope.contains(op->name)) {
            Interval bounds = scope.get(op->name);
            min = bounds.min;
            max = bounds.max;
        } else {
            debug(3) << op->name << " not in scope, so leaving it as-is\n";
            min = op;
            max = op;
        }
    }

    void visit(const Add *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        Expr min_b = min, max_b = max;

        if (min_a.same_as(op->a) && max_a.same_as(op->a) &&
            min_b.same_as(op->b) && max_b.same_as(op->b)) {
            min = max = op;
            return;
        }

        min = (min_b.defined() && min_a.defined()) ? Add::make(min_a, min_b) : Expr();

        if (min_a.same_as(max_a) && min_b.same_as(max_b)) {
            max = min;
        } else {
            max = (max_b.defined() && max_a.defined()) ? Add::make(max_a, max_b) : Expr();
        }

        // Check for overflow for (u)int8 and (u)int16
        if (!op->type.is_float() && op->type.bits < 32) {
            if (max.defined()) {
                Expr test = (cast<int>(max_a) + cast<int>(max_b) - cast<int>(max));
                //debug(0) << "Attempting to prove: " << test << " -> " << simplify(test) << "\n";
                if (!is_zero(simplify(test))) {
                    bounds_of_type(op->type);
                    return;
                }
            }
            if (min.defined()) {
                Expr test = (cast<int>(min_a) + cast<int>(min_b) - cast<int>(min));
                //debug(0) << "Attempting to prove: " << test << " -> " << simplify(test) << "\n";
                if (!is_zero(simplify(test))) {
                    bounds_of_type(op->type);
                    return;
                }
            }
        }

    }

    void visit(const Sub *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        Expr min_b = min, max_b = max;

        if (min_a.same_as(op->a) && max_a.same_as(op->a) &&
            min_b.same_as(op->b) && max_b.same_as(op->b)) {
            min = max = op;
            return;
        }

        min = (max_b.defined() && min_a.defined()) ? Sub::make(min_a, max_b) : Expr();
        if (min_a.same_as(max_a) && min_b.same_as(max_b)) {
            max = min;
        } else {
            max = (min_b.defined() && max_a.defined()) ? Sub::make(max_a, min_b) : Expr();
        }

        // Check for overflow for (u)int8 and (u)int16
        if (!op->type.is_float() && op->type.bits < 32) {
            if (max.defined()) {
                Expr test = (cast<int>(max_a) - cast<int>(min_b) - cast<int>(max));
                //debug(0) << "Attempting to prove: " << test << " -> " << simplify(test) << "\n";
                if (!is_zero(simplify(test))) {
                    bounds_of_type(op->type);
                    return;
                }
            }
            if (min.defined()) {
                Expr test = (cast<int>(min_a) - cast<int>(max_b) - cast<int>(min));
                //debug(0) << "Attempting to prove: " << test << " -> " << simplify(test) << "\n";
                if (!is_zero(simplify(test))) {
                    bounds_of_type(op->type);
                    return;
                }
            }
        }

        // Check underflow for uint
        if (op->type.is_uint()) {
            if (min.defined()) {
                Expr test = (max_b <= min_a);
                if (!is_one(simplify(test))) {
                    bounds_of_type(op->type);
                    return;
                }
            }
        }
    }

    void visit(const Mul *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        if (!min_a.defined() || !max_a.defined()) {
            min = Expr(); max = Expr(); return;
        }

        op->b.accept(this);
        Expr min_b = min, max_b = max;
        if (!min_b.defined() || !max_b.defined()) {
            min = Expr(); max = Expr(); return;
        }

        if (min_a.same_as(op->a) && max_a.same_as(op->a) &&
            min_b.same_as(op->b) && max_b.same_as(op->b)) {
            min = max = op;
            return;
        }

        if (min_a.same_as(max_a) && min_b.same_as(max_b)) {
            // A and B are constant
            min = max = min_a * min_b;
        } else if (min_a.same_as(max_a)) {
            // A is constant
            if (is_zero(min_a)) {
                min = max = min_a;
            } else if (is_positive_const(min_a) || op->type.is_uint()) {
                min = min_b * min_a;
                max = max_b * min_a;
            } else if (is_negative_const(min_a)) {
                min = max_b * min_a;
                max = min_b * min_a;
            } else {
                // Sign of a is unknown
                Expr a = min_a * min_b;
                Expr b = min_a * max_b;
                Expr cmp = min_a >= make_zero(min_a.type());
                min = select(cmp, a, b);
                max = select(cmp, b, a);
            }
        } else if (min_b.same_as(max_b)) {
            // B is constant
            if (is_zero(min_b)) {
                min = max = min_a;
            } else if (is_positive_const(min_b) || op->type.is_uint()) {
                min = min_a * min_b;
                max = max_a * min_b;
            } else if (is_negative_const(min_b)) {
                min = max_a * min_b;
                max = min_a * min_b;
            } else {
                // Sign of b is unknown
                Expr a = min_b * min_a;
                Expr b = min_b * max_a;
                Expr cmp = min_b >= make_zero(min_b.type());
                min = select(cmp, a, b);
                max = select(cmp, b, a);
            }
        } else {

            Expr a = min_a * min_b;
            Expr b = min_a * max_b;
            Expr c = max_a * min_b;
            Expr d = max_a * max_b;

            min = Min::make(Min::make(a, b), Min::make(c, d));
            max = Max::make(Max::make(a, b), Max::make(c, d));
        }

        if (op->type.bits < 32 && !op->type.is_float()) {
            // Try to prove it can't overflow
            Expr test1 = (cast<int>(min_a) * cast<int>(min_b) - cast<int>(min_a * min_b));
            Expr test2 = (cast<int>(min_a) * cast<int>(max_b) - cast<int>(min_a * max_b));
            Expr test3 = (cast<int>(max_a) * cast<int>(min_b) - cast<int>(max_a * min_b));
            Expr test4 = (cast<int>(max_a) * cast<int>(max_b) - cast<int>(max_a * max_b));
            test1 = simplify(test1);
            test2 = simplify(test2);
            test3 = simplify(test3);
            test4 = simplify(test4);
            if (!is_zero(test1) || !is_zero(test2) || !is_zero(test3) || !is_zero(test4)) {
                bounds_of_type(op->type);
                return;
            }
        }

    }

    void visit(const Div *op) {

        op->a.accept(this);
        Expr min_a = min, max_a = max;
        if (!min_a.defined() || !max_a.defined()) {
            min = Expr(); max = Expr(); return;
        }

        op->b.accept(this);
        Expr min_b = min, max_b = max;
        if (!min_b.defined() || !max_b.defined()) {
            min = Expr(); max = Expr(); return;
        }

        if (min_a.same_as(op->a) && max_a.same_as(op->a) &&
            min_b.same_as(op->b) && max_b.same_as(op->b)) {
            min = max = op;
            return;
        }

        if (equal(min_b, max_b)) {
            if (is_zero(min_b)) {
                // Divide by zero. Drat.
                min = Expr();
                max = Expr();
            } else if (is_positive_const(min_b) || op->type.is_uint()) {
                min = min_a / min_b;
                max = max_a / min_b;
            } else if (is_negative_const(min_b)) {
                min = max_a / min_b;
                max = min_a / min_b;
            } else {
                // Sign of b is unknown
                Expr a = min_a / min_b;
                Expr b = max_a / max_b;
                Expr cmp = min_b > make_zero(min_b.type());
                min = select(cmp, a, b);
                max = select(cmp, b, a);
            }
        } else {
            // if we can't statically prove that the divisor can't span zero, then we're unbounded
            int min_sign = static_sign(min_b);
            int max_sign = static_sign(max_b);
            if (min_sign != max_sign || min_sign == 0 || max_sign == 0) {
                min = Expr();
                max = Expr();
                return;
            }

            // Divisor is either strictly positive or strictly
            // negative, so we can just take the extrema.
            Expr a = min_a / min_b;
            Expr b = min_a / max_b;
            Expr c = max_a / min_b;
            Expr d = max_a / max_b;

            min = Min::make(Min::make(a, b), Min::make(c, d));
            max = Max::make(Max::make(a, b), Max::make(c, d));
        }
    }

    void visit(const Mod *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;

        op->b.accept(this);
        Expr min_b = min, max_b = max;
        if (!min_b.defined() || !max_b.defined()) {
            min = Expr(); max = Expr(); return;
        }

        if (min_a.same_as(op->a) && max_a.same_as(op->a) &&
            min_b.same_as(op->b) && max_b.same_as(op->b)) {
            min = max = op;
            return;
        }

        if (min_a.defined() && min_a.same_as(max_a) && min_b.same_as(max_b)) {
            min = max = Mod::make(min_a, min_b);
        } else {
            // Only consider B (so A can be undefined)
            min = make_zero(op->type);
            if (max_b.type().is_uint()) {
                max = max_b;
            } else {
                max = cast(op->type, abs(max_b));
            }
            if (!max.type().is_float()) {
                // Integer modulo returns at most one less than the
                // second arg.
                max = max - make_one(op->type);
            }
        }
    }

    void visit(const Min *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        Expr min_b = min, max_b = max;

        debug(3) << "Bounds of " << Expr(op) << "\n";

        if (min_a.same_as(op->a) && max_a.same_as(op->a) &&
            min_b.same_as(op->b) && max_b.same_as(op->b)) {
            min = max = op;
            return;
        }

        if (min_a.defined() && min_a.same_as(min_b) &&
            max_a.defined() && max_a.same_as(max_b)) {
            min = min_a;
            max = max_a;
            return;
        }

        if (min_a.defined() && min_b.defined()) {
            min = Min::make(min_a, min_b);
        } else {
            min = Expr();
        }

        if (max_a.defined() && max_b.defined()) {
            max = Min::make(max_a, max_b);
        } else {
            max = max_a.defined() ? max_a : max_b;
        }

        debug(3) << min << ", " << max << "\n";
    }


    void visit(const Max *op) {
        op->a.accept(this);
        Expr min_a = min, max_a = max;
        op->b.accept(this);
        Expr min_b = min, max_b = max;

        debug(3) << "Bounds of " << Expr(op) << "\n";

        if (min_a.same_as(op->a) && max_a.same_as(op->a) &&
            min_b.same_as(op->b) && max_b.same_as(op->b)) {
            min = max = op;
            return;
        }

        if (min_a.defined() && min_a.same_as(min_b) &&
            max_a.defined() && max_a.same_as(max_b)) {
            min = min_a;
            max = max_a;
            return;
        }

        if (min_a.defined() && min_b.defined()) {
            min = Max::make(min_a, min_b);
        } else {
            min = min_a.defined() ? min_a : min_b;
        }

        if (max_a.defined() && max_b.defined()) {
            max = Max::make(max_a, max_b);
        } else {
            max = Expr();
        }

        debug(3) << min << ", " << max << "\n";
    }

    void visit(const EQ *) {
        min = Expr();
        max = Expr();
    }

    void visit(const NE *) {
        min = Expr();
        max = Expr();
    }

    void visit(const LT *) {
        min = Expr();
        max = Expr();
    }

    void visit(const LE *) {
        min = Expr();
        max = Expr();
    }

    void visit(const GT *) {
        min = Expr();
        max = Expr();
    }

    void visit(const GE *) {
        min = Expr();
        max = Expr();
    }

    void visit(const And *) {
        min = Expr();
        max = Expr();
    }

    void visit(const Or *) {
        min = Expr();
        max = Expr();
    }

    void visit(const Not *) {
        min = Expr();
        max = Expr();
    }

    void visit(const Select *op) {
        op->true_value.accept(this);
        Expr min_a = min, max_a = max;
        if (!min_a.defined() || !max_a.defined()) {
            min = Expr(); max = Expr(); return;
        }

        op->false_value.accept(this);
        Expr min_b = min, max_b = max;
        if (!min_b.defined() || !max_b.defined()) {
            min = Expr(); max = Expr(); return;
        }

        if (min_a.same_as(min_b)) {
            min = min_a;
        } else {
            min = Min::make(min_a, min_b);
        }

        if (max_a.same_as(max_b)) {
            max = max_a;
        } else {
            max = Max::make(max_a, max_b);
        }
    }

    void visit(const Load *op) {
        op->index.accept(this);
        if (min.defined() && min.same_as(max)) {
            // If the index is const we can return the load of that index
            min = max = Load::make(op->type, op->name, min, op->image, op->param);
        } else {
            // Otherwise use the bounds of the type
            bounds_of_type(op->type);
        }
    }

    void visit(const Ramp *op) {
        internal_error << "Bounds of vector";
    }

    void visit(const Broadcast *) {
        internal_error << "Bounds of vector";
    }

    void visit(const Call *op) {
        // If the args are const we can return the call of those args
        // for pure functions (extern and image). For other types of
        // functions, the same call in two different places might
        // produce different results (e.g. during the update step of a
        // reduction), so we can't move around call nodes.
        std::vector<Expr> new_args(op->args.size());
        bool const_args = true;
        for (size_t i = 0; i < op->args.size() && const_args; i++) {
            op->args[i].accept(this);
            if (min.defined() && min.same_as(max)) {
                new_args[i] = min;
            } else {
                const_args = false;
            }
        }

        if (op->type == Handle()) {
            min = max = Expr();
            return;
        }

        if (const_args && (op->call_type == Call::Image || op->call_type == Call::Extern)) {
            min = max = Call::make(op->type, op->name, new_args, op->call_type,
                                   op->func, op->value_index, op->image, op->param);
        } else if (op->call_type == Call::Intrinsic && op->name == Call::abs) {
            Expr min_a = min, max_a = max;
            min = make_zero(op->type);
            if (min_a.defined() && max_a.defined()) {
                if (equal(min_a, max_a)) {
                    min = max = Call::make(op->type, Call::abs, vec(max_a), Call::Intrinsic);
                } else {
                    min = make_zero(op->type);
                    if (op->args[0].type().is_int() && op->args[0].type().bits == 32) {
                        max = Max::make(Cast::make(op->type, -min_a), Cast::make(op->type, max_a));
                    } else {
                        min_a = Call::make(op->type, Call::abs, vec(min_a), Call::Intrinsic);
                        max_a = Call::make(op->type, Call::abs, vec(max_a), Call::Intrinsic);
                        max = Max::make(min_a, max_a);
                    }
                }
            } else {
                // If the argument is unbounded on one side, then the max is unbounded.
                max = Expr();
            }
        } else if (op->call_type == Call::Intrinsic && op->name == Call::return_second) {
            assert(op->args.size() == 2);
            op->args[1].accept(this);
        } else if (op->call_type == Call::Intrinsic && op->name == Call::if_then_else) {
            assert(op->args.size() == 3);
            // Probably more conservative than necessary
            Expr equivalent_select = Select::make(op->args[0], op->args[1], op->args[2]);
            equivalent_select.accept(this);
        } else if (op->call_type == Call::Intrinsic && 
                   (op->name == Call::shift_left || op->name == Call::shift_right || op->name == Call::bitwise_and)) {
            Expr simplified = simplify(op);
            if (!simplified.same_as(op)) {
                simplified.accept(this);
            } else {
                // Just use the bounds of the type
                bounds_of_type(op->type);
            }
        } else if (op->args.size() == 1 && min.defined() && max.defined() &&
                   (op->name == "ceil_f32" || op->name == "ceil_f64" ||
                    op->name == "floor_f32" || op->name == "floor_f64" ||
                    op->name == "round_f32" || op->name == "round_f64" ||
                    op->name == "exp_f32" || op->name == "exp_f64" ||
                    op->name == "log_f32" || op->name == "log_f64")) {
            // For monotonic, pure, single-argument functions, we can
            // make two calls for the min and the max.
            Expr min_a = min, max_a = max;
            min = Call::make(op->type, op->name, vec<Expr>(min_a), op->call_type,
                             op->func, op->value_index, op->image, op->param);
            max = Call::make(op->type, op->name, vec<Expr>(max_a), op->call_type,
                             op->func, op->value_index, op->image, op->param);

        } else if (op->call_type == Call::Intrinsic &&
                   (op->name == Call::extract_buffer_min ||
                    op->name == Call::extract_buffer_max) &&
                   !op->args.empty() &&
                   op->args[0].as<Variable>()) {
            // Bounds query results should have perfect nesting. Their
            // max over a loop is just the same bounds query call at
            // an outer loop level. This requires that the query is
            // also done at the outer loop level so that the buffer
            // arg is still valid, which it is, so it is.
            //
            // TODO: There should be an assert injected in the inner
            // loop to check perfect nesting.
            min = Call::make(Int(32), Call::extract_buffer_min, op->args, Call::Intrinsic);
            max = Call::make(Int(32), Call::extract_buffer_max, op->args, Call::Intrinsic);
        } else if (op->call_type == Call::Intrinsic && op->name == Call::memoize_expr) {
            internal_assert(op->args.size() >= 1);
            op->args[0].accept(this);
        } else if (op->call_type == Call::Intrinsic && op->name == Call::trace_expr) {
            // trace_expr returns argument 4
            internal_assert(op->args.size() >= 5);
            op->args[4].accept(this);
        } else if (op->func.has_pure_definition()) {
            bounds_of_func(op->func, op->value_index);
        } else {
            // Just use the bounds of the type
            bounds_of_type(op->type);
        }
    }

    void visit(const Let *op) {
        op->value.accept(this);
        scope.push(op->name, Interval(min, max));
        op->body.accept(this);
        scope.pop(op->name);
    }

    void visit(const LetStmt *) {
        internal_error << "Bounds of statement\n";
    }

    void visit(const AssertStmt *) {
        internal_error << "Bounds of statement\n";
    }

    void visit(const Pipeline *) {
        internal_error << "Bounds of statement\n";
    }

    void visit(const For *) {
        internal_error << "Bounds of statement\n";
    }

    void visit(const Store *) {
        internal_error << "Bounds of statement\n";
    }

    void visit(const Provide *) {
        internal_error << "Bounds of statement\n";
    }

    void visit(const Allocate *) {
        internal_error << "Bounds of statement\n";
    }

    void visit(const Realize *) {
        internal_error << "Bounds of statement\n";
    }

    void visit(const Block *) {
        internal_error << "Bounds of statement\n";
    }
};

Interval bounds_of_expr_in_scope(Expr expr, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    //debug(3) << "computing bounds_of_expr_in_scope " << expr << "\n";
    Bounds b(&scope, fb);
    expr.accept(&b);
    //debug(3) << "bounds_of_expr_in_scope " << expr << " = " << simplify(b.min) << ", " << simplify(b.max) << "\n";
    return Interval(b.min, b.max);
}

Interval interval_union(const Interval &a, const Interval &b) {
    Expr max, min;
    debug(3) << "Interval union of " << a.min << ", " << a.max << ",  " << b.min << ", " << b.max << "\n";
    if (a.max.defined() && b.max.defined()) max = Max::make(a.max, b.max);
    if (a.min.defined() && b.min.defined()) min = Min::make(a.min, b.min);
    return Interval(min, max);
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

Expr simple_min(Expr a, Expr b) {
    // Take a min, doing a little bit of eager simplification
    if (equal(a, b)) {
        return a;
    }

    const IntImm *ia = a.as<IntImm>();
    const IntImm *ib = b.as<IntImm>();
    const Min *ma = a.as<Min>();
    const IntImm *imab = ma ? ma->b.as<IntImm>() : NULL;
    if (ia && ib) {
        if (ia->value < ib->value) {
            // min(3, 4) -> 3
            return ia;
        } else {
            // min(4, 3) -> 3
            return ib;
        }
    } else if (imab && ib) {
        if (imab->value < ib->value) {
            // min(min(a, 3), 4) -> min(a, 3)
            return a;
        } else {
            // min(min(a, 4), 3) -> min(a, 3)
            return min(ma->a, b);
        }
    } else if (ia) {
        // min(3, b) -> min(b, 3)
        return min(b, a);
    } else {
        return min(a, b);
    }
}

Expr simple_max(Expr a, Expr b) {
    // Take a max, doing a little bit of eager simplification
    if (equal(a, b)) {
        return a;
    }

    const IntImm *ia = a.as<IntImm>();
    const IntImm *ib = b.as<IntImm>();
    const Max *ma = a.as<Max>();
    const IntImm *imab = ma ? ma->b.as<IntImm>() : NULL;
    if (ia && ib) {
        if (ia->value > ib->value) {
            // max(4, 3) -> 4
            return ia;
        } else {
            // max(3, 4) -> 4
            return ib;
        }
    } else if (imab && ib) {
        if (imab->value > ib->value) {
            // max(max(a, 4), 3) -> max(a, 4)
            return a;
        } else {
            // max(max(a, 3), 4) -> max(a, 4)
            return max(ma->a, b);
        }
    } else if (ia) {
        // max(3, b) -> max(b, 3)
        return max(b, a);
    } else {
        return max(a, b);
    }
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

    for (size_t i = 0; i < a.size(); i++) {
        if (!a[i].min.same_as(b[i].min)) {
            if (a[i].min.defined() && b[i].min.defined()) {
                if (a_maybe_unused && b_maybe_unused) {
                    if (equal(a.used, !b.used) || equal(!a.used, b.used)) {
                        a[i].min = select(a.used, a[i].min, b[i].min);
                    } else {
                        a[i].min = select(a.used && b.used, simple_min(a[i].min, b[i].min),
                                          a.used, a[i].min,
                                          b[i].min);
                    }
                } else if (a_maybe_unused) {
                    a[i].min = select(a.used, simple_min(a[i].min, b[i].min), b[i].min);
                } else if (b_maybe_unused) {
                    a[i].min = select(b.used, simple_min(a[i].min, b[i].min), a[i].min);
                } else {
                    a[i].min = simple_min(a[i].min, b[i].min);
                }
            } else {
                a[i].min = Expr();
            }
        }
        if (!a[i].max.same_as(b[i].max)) {
            if (a[i].max.defined() && b[i].max.defined()) {
                if (a_maybe_unused && b_maybe_unused) {
                    if (equal(a.used, !b.used) || equal(!a.used, b.used)) {
                        a[i].max = select(a.used, a[i].max, b[i].max);
                    } else {
                        a[i].max = select(a.used && b.used, simple_max(a[i].max, b[i].max),
                                          a.used, a[i].max,
                                          b[i].max);
                    }
                } else if (a_maybe_unused) {
                    a[i].max = select(a.used, simple_max(a[i].max, b[i].max), b[i].max);
                } else if (b_maybe_unused) {
                    a[i].max = select(b.used, simple_max(a[i].max, b[i].max), a[i].max);
                } else {
                    a[i].max = simple_max(a[i].max, b[i].max);
                }
            } else {
                a[i].max = Expr();
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

bool boxes_overlap(const Box &a, const Box &b) {
    // If one box is scalar and the other is not, the boxes cannot
    // overlap.
    if (a.size() != b.size() && (a.size() == 0 || b.size() == 0)) {
        return false;
    }

    internal_assert(a.size() == b.size());

    bool a_maybe_unused = a.maybe_unused();
    bool b_maybe_unused = b.maybe_unused();

    // Overlapping requires both boxes to be used.
    Expr overlap = ((a_maybe_unused ? a.used : const_true()) &&
                    (b_maybe_unused ? b.used : const_true()));

    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].max.defined() && b[i].min.defined()) {
            overlap = overlap && b[i].max >= a[i].min;
        }
        if (a[i].min.defined() && b[i].max.defined()) {
            overlap = overlap && a[i].max >= b[i].min;
        }
    }

    return !is_zero(simplify(overlap));
}

// Compute the box produced by a statement
class BoxesTouched : public IRGraphVisitor {

public:
    BoxesTouched(bool calls, bool provides, string fn, const Scope<Interval> *s, const FuncValueBounds &fb) :
        func(fn), consider_calls(calls), consider_provides(provides), func_bounds(fb) {
        scope.set_containing_scope(s);
    }

    map<string, Box> boxes;

private:

    string func;
    bool consider_calls, consider_provides;
    Scope<Interval> scope;
    const FuncValueBounds &func_bounds;

    using IRGraphVisitor::visit;

    void visit(const Let *op) {
        if (!consider_calls) return;

        op->value.accept(this);
        Interval value_bounds = bounds_of_expr_in_scope(op->value, scope, func_bounds);
        scope.push(op->name, value_bounds);
        op->body.accept(this);
        scope.pop(op->name);
    }

    void visit(const Call *op) {
        if (!consider_calls) return;

        // Calls inside of an address_of aren't touched, because no
        // actual memory access takes place.
        if (op->call_type == Call::Intrinsic && op->name == Call::address_of) {
            // Visit the args of the inner call
            internal_assert(op->args.size() == 1);
            const Call *c = op->args[0].as<Call>();

            if (c) {
                for (size_t i = 0; i < c->args.size(); i++) {
                    c->args[i].accept(this);
                }
            } else {
                const Load *l = op->args[0].as<Load>();

                internal_assert(l);
                l->index.accept(this);
            }

            return;
        }

        IRVisitor::visit(op);

        if (op->call_type == Call::Intrinsic ||
            op->call_type == Call::Extern) {
            return;
        }

        Box b(op->args.size());
        b.used = const_true();
        for (size_t i = 0; i < op->args.size(); i++) {
            op->args[i].accept(this);
            b[i] = bounds_of_expr_in_scope(op->args[i], scope, func_bounds);
        }
        merge_boxes(boxes[op->name], b);
    }

    class CountVars : public IRVisitor {
        using IRVisitor::visit;

        void visit(const Variable *var) {
            count++;
        }
    public:
        int count;
        CountVars() : count(0) {}
    };

    // We get better simplification if we directly substitute mins
    // and maxes in, but this can also cause combinatorial code
    // explosion. Here we manage this by only substituting in
    // reasonably-sized expressions. We determine the size by
    // counting the number of var nodes.
    bool is_small_enough_to_substitute(Expr e) {
        if (!e.defined()) {
            return true;
        }
        CountVars c;
        e.accept(&c);
        return c.count < 10;
    }

    void visit(const LetStmt *op) {
        if (consider_calls) {
            op->value.accept(this);
        }
        Interval value_bounds = bounds_of_expr_in_scope(op->value, scope, func_bounds);
        value_bounds.min = simplify(value_bounds.min);
        value_bounds.max = simplify(value_bounds.max);

        if (is_small_enough_to_substitute(value_bounds.min) &&
            is_small_enough_to_substitute(value_bounds.max)) {
            scope.push(op->name, value_bounds);
            op->body.accept(this);
            scope.pop(op->name);
        } else {
            string max_name = unique_name('t');
            string min_name = unique_name('t');

            scope.push(op->name, Interval(Variable::make(op->value.type(), min_name),
                                          Variable::make(op->value.type(), max_name)));
            op->body.accept(this);
            scope.pop(op->name);

            for (map<string, Box>::iterator iter = boxes.begin();
                 iter != boxes.end(); ++iter) {
                Box &box = iter->second;
                for (size_t i = 0; i < box.size(); i++) {
                    if (box[i].min.defined()) {
                        box[i].min = Let::make(max_name, value_bounds.max, box[i].min);
                        box[i].min = Let::make(min_name, value_bounds.min, box[i].min);
                    }
                    if (box[i].max.defined()) {
                        box[i].max = Let::make(max_name, value_bounds.max, box[i].max);
                        box[i].max = Let::make(min_name, value_bounds.min, box[i].max);
                    }
                }
            }
        }
    }

    void visit(const IfThenElse *op) {
        op->condition.accept(this);

        if (expr_uses_vars(op->condition, scope)) {
            op->then_case.accept(this);
            if (op->else_case.defined()) {
                op->else_case.accept(this);
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

            //debug(0) << "Encountered an ifthenelse over a param: " << op->condition << "\n";

            // Make sure all the then boxes have an entry on the else
            // side so that the merge doesn't skip them.
            for (map<string, Box>::iterator iter = then_boxes.begin();
                 iter != then_boxes.end(); ++iter) {
                else_boxes[iter->first];
            }

            // Merge
            for (map<string, Box>::iterator iter = else_boxes.begin();
                 iter != else_boxes.end(); ++iter) {
                Box &else_box = iter->second;
                Box &then_box = then_boxes[iter->first];
                Box &orig_box = boxes[iter->first];

                //debug(0) << " Merging boxes for " << iter->first << "\n";

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

                merge_boxes(orig_box, then_box);
                merge_boxes(orig_box, else_box);
            }
        }

    }

    void visit(const For *op) {
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

        scope.push(op->name, Interval(min_val, max_val));
        op->body.accept(this);
        scope.pop(op->name);
    }

    void visit(const Provide *op) {
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
};

map<string, Box> boxes_touched(Expr e, Stmt s, bool consider_calls, bool consider_provides,
                               string fn, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    BoxesTouched b(consider_calls, consider_provides, fn, &scope, fb);
    if (e.defined()) {
        e.accept(&b);
    }
    if (s.defined()) {
        s.accept(&b);
    }
    return b.boxes;
}

Box box_touched(Expr e, Stmt s, bool consider_calls, bool consider_provides,
                string fn, const Scope<Interval> &scope, const FuncValueBounds &fb) {
    return boxes_touched(e, s, consider_calls, consider_provides, fn, scope, fb)[fn];
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

void check(const Scope<Interval> &scope, Expr e, Expr correct_min, Expr correct_max) {
    FuncValueBounds fb;
    Interval result = bounds_of_expr_in_scope(e, scope, fb);
    if (result.min.defined()) result.min = simplify(result.min);
    if (result.max.defined()) result.max = simplify(result.max);
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

FuncValueBounds compute_function_value_bounds(const vector<string> &order,
                                              const map<string, Function> &env) {
    FuncValueBounds fb;

    for (size_t i = 0; i < order.size(); i++) {
        Function f = env.find(order[i])->second;
        for (int j = 0; j < f.outputs(); j++) {
            pair<string, int> key = make_pair(f.name(), j);

            Interval result;

            if (f.has_pure_definition() &&
                !f.has_update_definition() &&
                !f.has_extern_definition()) {

                // Make a scope that says the args could be anything.
                Scope<Interval> arg_scope;
                for (size_t k = 0; k < f.args().size(); k++) {
                    arg_scope.push(f.args()[k], Interval(Expr(), Expr()));
                }

                result = bounds_of_expr_in_scope(f.values()[j], arg_scope, fb);

                if (result.min.defined()) {
                    result.min = simplify(result.min);
                }

                if (result.max.defined()) {
                    result.max = simplify(result.max);
                }

                fb[key] = result;

            }

            debug(2) << "Bounds on value " << j
                     << " for func " << order[i]
                     << " are: " << result.min << ", " << result.max << "\n";
        }
    }

    return fb;
}

void bounds_test() {
    Scope<Interval> scope;
    Var x("x"), y("y");
    scope.push("x", Interval(Expr(0), Expr(10)));

    check(scope, x, 0, 10);
    check(scope, x+1, 1, 11);
    check(scope, (x+1)*2, 2, 22);
    check(scope, x*x, 0, 100);
    check(scope, 5-x, -5, 5);
    check(scope, x*(5-x), -50, 50); // We don't expect bounds analysis to understand correlated terms
    check(scope, Select::make(x < 4, x, x+100), 0, 110);
    check(scope, x+y, y, y+10);
    check(scope, x*y, select(y < 0, y*10, 0), select(y < 0, 0, y*10));
    check(scope, x/(x+y), Expr(), Expr());
    check(scope, 11/(x+1), 1, 11);
    check(scope, Load::make(Int(8), "buf", x, Buffer(), Parameter()), cast(Int(8), -128), cast(Int(8), 127));
    check(scope, y + (Let::make("y", x+3, y - x + 10)), y + 3, y + 23); // Once again, we don't know that y is correlated with x
    check(scope, clamp(1/(x-2), x-10, x+10), -10, 20);

    check(scope, print(x, y), 0, 10);
    check(scope, print_when(x > y, x, y), 0, 10);

    // Check some operations that may overflow
    check(scope, (cast<uint8_t>(x)+250), cast<uint8_t>(0), cast<uint8_t>(255));
    check(scope, (cast<uint8_t>(x)+10)*20, cast<uint8_t>(0), cast<uint8_t>(255));
    check(scope, (cast<uint8_t>(x)+10)*(cast<uint8_t>(x)+5), cast<uint8_t>(0), cast<uint8_t>(255));
    check(scope, (cast<uint8_t>(x)+10)-(cast<uint8_t>(x)+5), cast<uint8_t>(0), cast<uint8_t>(255));

    // Check some operations that we should be able to prove do not overflow
    check(scope, (cast<uint8_t>(x)+240), cast<uint8_t>(240), cast<uint8_t>(250));
    check(scope, (cast<uint8_t>(x)+10)*10, cast<uint8_t>(100), cast<uint8_t>(200));
    check(scope, (cast<uint8_t>(x)+10)*(cast<uint8_t>(x)), cast<uint8_t>(0), cast<uint8_t>(200));
    check(scope, (cast<uint8_t>(x)+20)-(cast<uint8_t>(x)+5), cast<uint8_t>(5), cast<uint8_t>(25));

    Expr u8_1 = cast<uint8_t>(Load::make(Int(8), "buf", x, Buffer(), Parameter()));
    Expr u8_2 = cast<uint8_t>(Load::make(Int(8), "buf", x + 17, Buffer(), Parameter()));
    check(scope, cast<uint16_t>(u8_1) + cast<uint16_t>(u8_2),
          cast<uint16_t>(0), cast<uint16_t>(255*2));

    vector<Expr> input_site_1 = vec(2*x);
    vector<Expr> input_site_2 = vec(2*x+1);
    vector<Expr> output_site = vec(x+1);

    Buffer in(Int(32), vec(10), NULL, "input");

    Stmt loop = For::make("x", 3, 10, For::Serial,
                          Provide::make("output",
                                        vec(Add::make(
                                                Call::make(in, input_site_1),
                                                Call::make(in, input_site_2))),
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

    Box r2 = vec(Interval(Expr(5), Expr(19)));
    merge_boxes(r2, r["output"]);
    internal_assert(equal(simplify(r2[0].min), 4));
    internal_assert(equal(simplify(r2[0].max), 19));

    std::cout << "Bounds test passed" << std::endl;
}

}
}

#include "IROperator.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "../../src/IROperator.h"

#include <string>

namespace h = Halide;
namespace p = boost::python;
using p::self;


h::Expr reinterpret0(h::Type t, h::Expr e)
{
    return h::reinterpret(t, e);
}

h::Expr cast0(h::Type t, h::Expr e)
{
    return Halide::cast(t, e);
}

void defineOperators()
{
    // defined in IROperator.h

    p::def("max", &h::max,
           p::args("a", "b"),
           "Returns an expression representing the greater of the two "
           "arguments, after doing any necessary type coercion using "
           "\ref Internal::match_types. Vectorizes cleanly on most platforms "
           "(with the exception of integer types on x86 without SSE4).");

    p::def("min", &h::min,
           p::args("a", "b"),
           "Returns an expression representing the greater of the two "
           "arguments, after doing any necessary type coercion using "
           "\ref Internal::match_types. Vectorizes cleanly on most platforms "
           "(with the exception of integer types on x86 without SSE4).");

    p::def("clamp", &h::clamp,
           p::args("a", "min_val", "max_val"),
           "Clamps an expression to lie within the given bounds. The bounds "
           "are type-cast to match the expression. Vectorizes as well as min/max.");

    //    /** Returns the absolute value of a signed integer or floating-point
    //     * expression. Vectorizes cleanly. Unlike in C, abs of a signed
    //     * integer returns an unsigned integer of the same bit width. This
    //     * means that abs of the most negative integer doesn't overflow. */
    //    inline Expr abs(Expr a) {
    //        user_assert(a.defined())
    //            << "abs of undefined Expr\n";
    //        Type t = a.type();
    //        if (t.is_int()) {
    //            t.code = Type::UInt;
    //        } else if (t.is_uint()) {
    //            user_warning << "Warning: abs of an unsigned type is a no-op\n";
    //            return a;
    //        }
    //        return Internal::Call::make(t, Internal::Call::abs,
    //                                    {a}, Internal::Call::Intrinsic);
    //    }

    //    /** Return the absolute difference between two values. Vectorizes
    //     * cleanly. Returns an unsigned value of the same bit width. There are
    //     * various ways to write this yourself, but they contain numerous
    //     * gotchas and don't always compile to good code, so use this
    //     * instead. */
    //    inline Expr absd(Expr a, Expr b) {
    //        user_assert(a.defined() && b.defined()) << "absd of undefined Expr\n";
    //        Internal::match_types(a, b);
    //        Type t = a.type();

    //        if (t.is_float()) {
    //            // Floats can just use abs.
    //            return abs(a - b);
    //        }

    //        if (t.is_int()) {
    //            // The argument may be signed, but the return type is unsigned.
    //            t.code = Type::UInt;
    //        }

    //        return Internal::Call::make(t, Internal::Call::absd,
    //                                    {a, b},
    //                                    Internal::Call::Intrinsic);
    //    }

    //    /** Returns an expression similar to the ternary operator in C, except
    //     * that it always evaluates all arguments. If the first argument is
    //     * true, then return the second, else return the third. Typically
    //     * vectorizes cleanly, but benefits from SSE41 or newer on x86. */
    //    inline Expr select(Expr condition, Expr true_value, Expr false_value) {

    //        if (as_const_int(condition)) {
    //            // Why are you doing this? We'll preserve the select node until constant folding for you.
    //            condition = cast(Bool(), condition);
    //        }

    //        // Coerce int literals to the type of the other argument
    //        if (as_const_int(true_value)) {
    //            true_value = cast(false_value.type(), true_value);
    //        }
    //        if (as_const_int(false_value)) {
    //            false_value = cast(true_value.type(), false_value);
    //        }

    //        user_assert(condition.type().is_bool())
    //            << "The first argument to a select must be a boolean:\n"
    //            << "  " << condition << " has type " << condition.type() << "\n";

    //        user_assert(true_value.type() == false_value.type())
    //            << "The second and third arguments to a select do not have a matching type:\n"
    //            << "  " << true_value << " has type " << true_value.type() << "\n"
    //            << "  " << false_value << " has type " << false_value.type() << "\n";

    //        return Internal::Select::make(condition, true_value, false_value);
    //    }

    //    /** A multi-way variant of select similar to a switch statement in C,
    //     * which can accept multiple conditions and values in pairs. Evaluates
    //     * to the first value for which the condition is true. Returns the
    //     * final value if all conditions are false. */
    //    // @{
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      select(c2, v2, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      select(c3, v3, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      select(c4, v4, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr c5, Expr v5,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      c4, v4,
    //                      select(c5, v5, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr c5, Expr v5,
    //                       Expr c6, Expr v6,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      c4, v4,
    //                      c5, v5,
    //                      select(c6, v6, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr c5, Expr v5,
    //                       Expr c6, Expr v6,
    //                       Expr c7, Expr v7,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      c4, v4,
    //                      c5, v5,
    //                      c6, v6,
    //                      select(c7, v7, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr c5, Expr v5,
    //                       Expr c6, Expr v6,
    //                       Expr c7, Expr v7,
    //                       Expr c8, Expr v8,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      c4, v4,
    //                      c5, v5,
    //                      c6, v6,
    //                      c7, v7,
    //                      select(c8, v8, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr c5, Expr v5,
    //                       Expr c6, Expr v6,
    //                       Expr c7, Expr v7,
    //                       Expr c8, Expr v8,
    //                       Expr c9, Expr v9,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      c4, v4,
    //                      c5, v5,
    //                      c6, v6,
    //                      c7, v7,
    //                      c8, v8,
    //                      select(c9, v9, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr c5, Expr v5,
    //                       Expr c6, Expr v6,
    //                       Expr c7, Expr v7,
    //                       Expr c8, Expr v8,
    //                       Expr c9, Expr v9,
    //                       Expr c10, Expr v10,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      c4, v4,
    //                      c5, v5,
    //                      c6, v6,
    //                      c7, v7,
    //                      c8, v8,
    //                      c9, v9,
    //                      select(c10, v10, default_val));
    //    }
    //    // @}

    // sin, cos, tan @{
    p::def("sin", &h::sin, p::args("x"),
           "Return the sine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("asin", &h::asin, p::args("x"),
           "Return the arcsine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("cos", &h::cos, p::args("x"),
           "Return the cosine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("acos", &h::acos, p::args("x"),
           "Return the arccosine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("tan", &h::tan, p::args("x"),
           "Return the tangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("atan", &h::atan, p::args("x"),
           "Return the arctangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("atan", &h::atan2, p::args("x", "y"),
           "Return the arctangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("atan2", &h::atan2, p::args("x", "y"),
           "Return the arctangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");
    // @}

    // sinh, cosh, tanh @{
    p::def("sinh", &h::sinh, p::args("x"),
           "Return the hyperbolic sine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("asinh", &h::asinh, p::args("x"),
           "Return the hyperbolic arcsine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");


    p::def("cosh", &h::cosh, p::args("x"),
           "Return the hyperbolic cosine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("acosh", &h::acosh, p::args("x"),
           "Return the hyperbolic arccosine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("tanh", &h::tanh, p::args("x"),
           "Return the hyperbolic tangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("atanh", &h::atanh, p::args("x"),
           "Return the hyperbolic arctangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");
    // @}



    //    /** Return the square root of a floating-point expression. If the
    //     * argument is not floating-point, it is cast to Float(32). Typically
    //     * vectorizes cleanly. */
    //    inline Expr sqrt(Expr x) {
    //        user_assert(x.defined()) << "sqrt of undefined Expr\n";
    //        if (x.type() == Float(64)) {
    //            return Internal::Call::make(Float(64), "sqrt_f64", {x}, Internal::Call::Extern);
    //        } else {
    //            return Internal::Call::make(Float(32), "sqrt_f32", {cast<float>(x)}, Internal::Call::Extern);
    //        }
    //    }

    //    /** Return the square root of the sum of the squares of two
    //     * floating-point expressions. If the argument is not floating-point,
    //     * it is cast to Float(32). Vectorizes cleanly. */
    //    inline Expr hypot(Expr x, Expr y) {
    //        return sqrt(x*x + y*y);
    //    }

    //    /** Return the exponential of a floating-point expression. If the
    //     * argument is not floating-point, it is cast to Float(32). For
    //     * Float(64) arguments, this calls the system exp function, and does
    //     * not vectorize well. For Float(32) arguments, this function is
    //     * vectorizable, does the right thing for extremely small or extremely
    //     * large inputs, and is accurate up to the last bit of the
    //     * mantissa. Vectorizes cleanly. */
    //    inline Expr exp(Expr x) {
    //        user_assert(x.defined()) << "exp of undefined Expr\n";
    //        if (x.type() == Float(64)) {
    //            return Internal::Call::make(Float(64), "exp_f64", {x}, Internal::Call::Extern);
    //        } else {
    //            return Internal::Call::make(Float(32), "exp_f32", {cast<float>(x)}, Internal::Call::Extern);
    //        }
    //    }

    //    /** Return the logarithm of a floating-point expression. If the
    //     * argument is not floating-point, it is cast to Float(32). For
    //     * Float(64) arguments, this calls the system log function, and does
    //     * not vectorize well. For Float(32) arguments, this function is
    //     * vectorizable, does the right thing for inputs <= 0 (returns -inf or
    //     * nan), and is accurate up to the last bit of the
    //     * mantissa. Vectorizes cleanly. */
    //    inline Expr log(Expr x) {
    //        user_assert(x.defined()) << "log of undefined Expr\n";
    //        if (x.type() == Float(64)) {
    //            return Internal::Call::make(Float(64), "log_f64", {x}, Internal::Call::Extern);
    //        } else {
    //            return Internal::Call::make(Float(32), "log_f32", {cast<float>(x)}, Internal::Call::Extern);
    //        }
    //    }

    //    /** Return one floating point expression raised to the power of
    //     * another. The type of the result is given by the type of the first
    //     * argument. If the first argument is not a floating-point type, it is
    //     * cast to Float(32). For Float(32), cleanly vectorizable, and
    //     * accurate up to the last few bits of the mantissa. Gets worse when
    //     * approaching overflow. Vectorizes cleanly. */
    //    inline Expr pow(Expr x, Expr y) {
    //        user_assert(x.defined() && y.defined()) << "pow of undefined Expr\n";

    //        if (const int *i = as_const_int(y)) {
    //            return raise_to_integer_power(x, *i);
    //        }

    //        if (x.type() == Float(64)) {
    //            y = cast<double>(y);
    //            return Internal::Call::make(Float(64), "pow_f64", {x, y}, Internal::Call::Extern);
    //        } else {
    //            x = cast<float>(x);
    //            y = cast<float>(y);
    //            return Internal::Call::make(Float(32), "pow_f32", {x, y}, Internal::Call::Extern);
    //        }
    //    }

    //    /** Evaluate the error function erf. Only available for
    //     * Float(32). Accurate up to the last three bits of the
    //     * mantissa. Vectorizes cleanly. */
    //    inline Expr erf(Expr x) {
    //        user_assert(x.defined()) << "erf of undefined Expr\n";
    //        user_assert(x.type() == Float(32)) << "erf only takes float arguments\n";
    //        return Internal::halide_erf(x);
    //    }

    //    /** Fast approximate cleanly vectorizable log for Float(32). Returns
    //     * nonsense for x <= 0.0f. Accurate up to the last 5 bits of the
    //     * mantissa. Vectorizes cleanly. */
    //    EXPORT Expr fast_log(Expr x);

    //    /** Fast approximate cleanly vectorizable exp for Float(32). Returns
    //     * nonsense for inputs that would overflow or underflow. Typically
    //     * accurate up to the last 5 bits of the mantissa. Gets worse when
    //     * approaching overflow. Vectorizes cleanly. */
    //    EXPORT Expr fast_exp(Expr x);

    //    /** Fast approximate cleanly vectorizable pow for Float(32). Returns
    //     * nonsense for x < 0.0f. Accurate up to the last 5 bits of the
    //     * mantissa for typical exponents. Gets worse when approaching
    //     * overflow. Vectorizes cleanly. */
    //    inline Expr fast_pow(Expr x, Expr y) {
    //        if (const int *i = as_const_int(y)) {
    //            return raise_to_integer_power(x, *i);
    //        }

    //        x = cast<float>(x);
    //        y = cast<float>(y);
    //        return select(x == 0.0f, 0.0f, fast_exp(fast_log(x) * y));
    //    }

    //    /** Fast approximate inverse for Float(32). Corresponds to the rcpps
    //     * instruction on x86, and the vrecpe instruction on ARM. Vectorizes
    //     * cleanly. */
    //    inline Expr fast_inverse(Expr x) {
    //        user_assert(x.type() == Float(32)) << "fast_inverse only takes float arguments\n";
    //        return Internal::Call::make(x.type(), "fast_inverse_f32", {x}, Internal::Call::Extern);
    //    }

    //    /** Fast approximate inverse square root for Float(32). Corresponds to
    //     * the rsqrtps instruction on x86, and the vrsqrte instruction on
    //     * ARM. Vectorizes cleanly. */
    //    inline Expr fast_inverse_sqrt(Expr x) {
    //        user_assert(x.type() == Float(32)) << "fast_inverse_sqrt only takes float arguments\n";
    //        return Internal::Call::make(x.type(), "fast_inverse_sqrt_f32", {x}, Internal::Call::Extern);
    //    }



    p::def("floor", &h::floor, p::args("x"),
           "Return the greatest whole number less than or equal to a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    p::def("ceil", &h::ceil, p::args("x"),
           "Return the least whole number less than or equal to a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    p::def("round", &h::round, p::args("x"),
           "Return the whole number closest to a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    p::def("trunc", &h::trunc, p::args("x"),
           "Return the integer part of a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    p::def("fract", &h::fract, p::args("x"),
           "Return the fractional part of a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    p::def("is_nan", &h::is_nan, p::args("x"),
           "Returns true if the argument is a Not a Number (NaN). "
           "Requires a floating point argument.  Vectorizes cleanly.");

    p::def("reinterpret", &reinterpret0, p::args("t, e"),
           "Reinterpret the bits of one value as another type.");

    p::def("cast", &cast0, p::args("t, e"),
           "Cast an expression to a new type.");

    return;
}

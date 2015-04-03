#ifndef HALIDE_IR_OPERATOR_H
#define HALIDE_IR_OPERATOR_H

/** \file
 *
 * Defines various operator overloads and utility functions that make
 * it more pleasant to work with Halide expressions.
 */

#include "IR.h"

namespace Halide {

namespace Internal {
/** Is the expression either an IntImm, a FloatImm, a StringImm, or a
 * Cast of the same, or a Ramp or Broadcast of the same. Doesn't do
 * any constant folding. */
EXPORT bool is_const(Expr e);

/** Is the expression an IntImm, FloatImm of a particular value, or a
 * Cast, or Broadcast of the same. */
EXPORT bool is_const(Expr e, int v);

/** If an expression is an IntImm, return a pointer to its
 * value. Otherwise returns NULL. */
EXPORT const int *as_const_int(Expr e);

/** If an expression is a FloatImm, return a pointer to its
 * value. Otherwise returns NULL. */
EXPORT const float *as_const_float(Expr e);

/** Is the expression a constant integer power of two. Also returns
 * log base two of the expression if it is. */
EXPORT bool is_const_power_of_two(Expr e, int *bits);

/** Is the expression a const (as defined by is_const), and also
 * strictly greater than zero (in all lanes, if a vector expression) */
EXPORT bool is_positive_const(Expr e);

/** Is the expression a const (as defined by is_const), and also
 * strictly less than zero (in all lanes, if a vector expression) */
EXPORT bool is_negative_const(Expr e);

/** Is the expression a const (as defined by is_const), and also
 * strictly less than zero (in all lanes, if a vector expression) and
 * is its negative value representable. (This excludes the most
 * negative value of the Expr's type from inclusion. Intended to be
 * used when the value will be negated as part of simplification.)
 */
EXPORT bool is_negative_negatable_const(Expr e);

/** Is the expression a const (as defined by is_const), and also equal
 * to zero (in all lanes, if a vector expression) */
EXPORT bool is_zero(Expr e);

/** Is the expression a const (as defined by is_const), and also equal
 * to one (in all lanes, if a vector expression) */
EXPORT bool is_one(Expr e);

/** Is the expression a const (as defined by is_const), and also equal
 * to two (in all lanes, if a vector expression) */
EXPORT bool is_two(Expr e);

/** Is the statement a no-op (which we represent as either an
 * undefined Stmt, or as an Evaluate node of a constant) */
EXPORT bool is_no_op(Stmt s);

/** Given an integer value, cast it into a designated integer type
 * and return the bits as int. Unsigned types are returned as bits in the int
 * and should be cast to unsigned int for comparison.
 * int_cast_constant implements bit manipulations to wrap val into the
 * value range of the Type t.
 * For example, int_cast_constant(UInt(16), -1) returns 65535
 * int_cast_constant(Int(8), 128) returns -128
 */
EXPORT int int_cast_constant(Type t, int val);

/** Construct a const of the given type */
EXPORT Expr make_const(Type t, int val);

/** Construct a boolean constant from a C++ boolean value.
 * May also be a vector if width is given.
 * It is not possible to coerce a C++ boolean to Expr because
 * if we provide such a path then char objects can ambiguously
 * be converted to Halide Expr or to std::string.  The problem
 * is that C++ does not have a real bool type - it is in fact
 * close enough to char that C++ does not know how to distinguish them.
 * make_bool is the explicit coercion. */
EXPORT Expr make_bool(bool val, int width = 1);

/** Construct the representation of zero in the given type */
EXPORT Expr make_zero(Type t);

/** Construct the representation of one in the given type */
EXPORT Expr make_one(Type t);

/** Construct the representation of two in the given type */
EXPORT Expr make_two(Type t);

/** Construct the constant boolean true. May also be a vector of
 * trues, if a width argument is given. */
EXPORT Expr const_true(int width = 1);

/** Construct the constant boolean false. May also be a vector of
 * falses, if a width argument is given. */
EXPORT Expr const_false(int width = 1);

/** Coerce the two expressions to have the same type, using C-style
 * casting rules. For the purposes of casting, a boolean type is
 * UInt(1). We use the following procedure:
 *
 * If the types already match, do nothing.
 *
 * Then, if one type is a vector and the other is a scalar, the scalar
 * is broadcast to match the vector width, and we continue.
 *
 * Then, if one type is floating-point and the other is not, the
 * non-float is cast to the floating-point type, and we're done.
 *
 * Then, if neither is a float but one of the two is a constant, the
 * constant is cast to match the non-const type and we're done. For
 * example, e has type UInt(8), then (e*32) also has type UInt(8),
 * despite the overflow that may occur. Note that this also means that
 * (e*(-1)) is positive, and is equivalent to (e*255) - i.e. the (-1)
 * is cast to a UInt(8) before the multiplication.
 *
 * Then, if both types are unsigned ints, the one with fewer bits is
 * cast to match the one with more bits and we're done.
 *
 * Then, if both types are signed ints, the one with fewer bits is
 * cast to match the one with more bits and we're done.
 *
 * Finally, if one type is an unsigned int and the other type is a signed
 * int, both are cast to a signed int with the greater of the two
 * bit-widths. For example, matching an Int(8) with a UInt(16) results
 * in an Int(16).
 *
 */
EXPORT void match_types(Expr &a, Expr &b);

/** Halide's vectorizable transcendentals. */
// @{
EXPORT Expr halide_log(Expr a);
EXPORT Expr halide_exp(Expr a);
EXPORT Expr halide_erf(Expr a);
// @}

/** Raise an expression to an integer power by repeatedly multiplying
 * it by itself. */
EXPORT Expr raise_to_integer_power(Expr a, int b);


}

/** Cast an expression to the halide type corresponding to the C++ type T. */
template<typename T>
inline Expr cast(Expr a) {
    return cast(type_of<T>(), a);
}

/** Cast an expression to a new type. */
inline Expr cast(Type t, Expr a) {
    user_assert(a.defined()) << "cast of undefined Expr\n";
    if (a.type() == t) return a;

    if (t.is_handle() && !a.type().is_handle()) {
        user_error << "Can't cast \"" << a << "\" to a handle. "
                   << "The only legal cast from scalar types to a handle is: "
                   << "reinterpret(Handle(), cast<uint64_t>(" << a << "));\n";
    } else if (a.type().is_handle() && !t.is_handle()) {
        user_error << "Can't cast handle \"" << a << "\" to type " << t << ". "
                   << "The only legal cast from handles to scalar types is: "
                   << "reinterpret(UInt64(), " << a << ");\n";
    }

    if (t.is_vector()) {
        if (a.type().is_scalar()) {
            return Internal::Broadcast::make(cast(t.element_of(), a), t.width);
        } else if (const Internal::Broadcast *b = a.as<Internal::Broadcast>()) {
            internal_assert(b->width == t.width);
            return Internal::Broadcast::make(cast(t.element_of(), b->value), t.width);
        }
    }
    return Internal::Cast::make(t, a);
}

/** Return the sum of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator+(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator+ of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::Add::make(a, b);
}

/** Modify the first expression to be the sum of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator+=(Expr &a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator+= of undefined Expr\n";
    a = Internal::Add::make(a, cast(a.type(), b));
    return a;
}

/** Return the difference of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator-(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator- of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::Sub::make(a, b);
}

/** Return the negative of the argument. Does no type casting, so more
 * formally: return that number which when added to the original,
 * yields zero of the same type. For unsigned integers the negative is
 * still an unsigned integer. E.g. in UInt(8), the negative of 56 is
 * 200, because 56 + 200 == 0 */
inline Expr operator-(Expr a) {
    user_assert(a.defined()) << "operator- of undefined Expr\n";
    return Internal::Sub::make(Internal::make_zero(a.type()), a);
}

/** Modify the first expression to be the difference of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator-=(Expr &a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator-= of undefined Expr\n";
    a = Internal::Sub::make(a, cast(a.type(), b));
    return a;
}

/** Return the product of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator*(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator* of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::Mul::make(a, b);
}

/** Modify the first expression to be the product of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator*=(Expr &a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator*= of undefined Expr\n";
    a = Internal::Mul::make(a, cast(a.type(), b));
    return a;
}

/** Return the ratio of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator/(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator/ of undefined Expr\n";
    user_assert(!Internal::is_const(b, 0)) << "operator/ with constant 0 divisor\n";
    Internal::match_types(a, b);
    return Internal::Div::make(a, b);
}

/** Modify the first expression to be the ratio of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator/=(Expr &a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator/= of undefined Expr\n";
    user_assert(!Internal::is_const(b, 0)) << "operator/= with constant 0 divisor\n";
    a = Internal::Div::make(a, cast(a.type(), b));
    return a;
}

/** Return the first argument reduced modulo the second, doing any
 * necessary type coercion using \ref Internal::match_types */
inline Expr operator%(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator% of undefined Expr\n";
    user_assert(!Internal::is_const(b, 0)) << "operator% with constant 0 modulus\n";
    Internal::match_types(a, b);
    return Internal::Mod::make(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is greater than the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator>(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator> of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::GT::make(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is less than the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator<(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator< of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::LT::make(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is less than or equal to the second, after doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator<=(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator<= of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::LE::make(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is greater than or equal to the second, after doing any necessary
 * type coercion using \ref Internal::match_types */

inline Expr operator>=(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator>= of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::GE::make(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is equal to the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator==(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator== of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::EQ::make(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is not equal to the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator!=(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator!= of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::NE::make(a, b);
}

/** Returns the logical and of the two arguments */
inline Expr operator&&(Expr a, Expr b) {
    return Internal::And::make(a, b);
}

/** Returns the logical or of the two arguments */
inline Expr operator||(Expr a, Expr b) {
    return Internal::Or::make(a, b);
}

/** Returns the logical not the argument */
inline Expr operator!(Expr a) {
    return Internal::Not::make(a);
}

/** Returns an expression representing the greater of the two
 * arguments, after doing any necessary type coercion using
 * \ref Internal::match_types. Vectorizes cleanly on most platforms
 * (with the exception of integer types on x86 without SSE4). */
inline Expr max(Expr a, Expr b) {
    user_assert(a.defined() && b.defined())
        << "max of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::Max::make(a, b);
}

/** Returns an expression representing the lesser of the two
 * arguments, after doing any necessary type coercion using
 * \ref Internal::match_types. Vectorizes cleanly on most platforms
 * (with the exception of integer types on x86 without SSE4). */
inline Expr min(Expr a, Expr b) {
    user_assert(a.defined() && b.defined())
        << "min of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::Min::make(a, b);
}

/** Clamps an expression to lie within the given bounds. The bounds
 * are type-cast to match the expression. Vectorizes as well as min/max. */
inline Expr clamp(Expr a, Expr min_val, Expr max_val) {
    user_assert(a.defined() && min_val.defined() && max_val.defined())
        << "clamp of undefined Expr\n";
    min_val = cast(a.type(), min_val);
    max_val = cast(a.type(), max_val);
    return Internal::Max::make(Internal::Min::make(a, max_val), min_val);
}

/** Returns the absolute value of a signed integer or floating-point
 * expression. Vectorizes cleanly. Unlike in C, abs of a signed
 * integer returns an unsigned integer of the same bit width. This
 * means that abs of the most negative integer doesn't overflow. */
inline Expr abs(Expr a) {
    user_assert(a.defined())
        << "abs of undefined Expr\n";
    Type t = a.type();
    if (t.is_int()) {
        t.code = Type::UInt;
    } else if (t.is_uint()) {
        user_warning << "Warning: abs of an unsigned type is a no-op\n";
        return a;
    }
    return Internal::Call::make(t, Internal::Call::abs,
                                vec(a), Internal::Call::Intrinsic);
}

/** Return the absolute difference between two values. Vectorizes
 * cleanly. Returns an unsigned value of the same bit width. There are
 * various ways to write this yourself, but they contain numerous
 * gotchas and don't always compile to good code, so use this
 * instead. */
inline Expr absd(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "absd of undefined Expr\n";
    Internal::match_types(a, b);
    Type t = a.type();

    if (t.is_float()) {
        // Floats can just use abs.
        return abs(a - b);
    }

    if (t.is_int()) {
        // The argument may be signed, but the return type is unsigned.
        t.code = Type::UInt;
    }

    return Internal::Call::make(t, Internal::Call::absd,
                                vec(a, b),
                                Internal::Call::Intrinsic);
}

/** Returns an expression similar to the ternary operator in C, except
 * that it always evaluates all arguments. If the first argument is
 * true, then return the second, else return the third. Typically
 * vectorizes cleanly, but benefits from SSE41 or newer on x86. */
inline Expr select(Expr condition, Expr true_value, Expr false_value) {

    if (as_const_int(condition)) {
        // Why are you doing this? We'll preserve the select node until constant folding for you.
        condition = cast(Bool(), condition);
    }

    // Coerce int literals to the type of the other argument
    if (as_const_int(true_value)) {
        true_value = cast(false_value.type(), true_value);
    }
    if (as_const_int(false_value)) {
        false_value = cast(true_value.type(), false_value);
    }

    user_assert(condition.type().is_bool())
        << "The first argument to a select must be a boolean:\n"
        << "  " << condition << " has type " << condition.type() << "\n";

    user_assert(true_value.type() == false_value.type())
        << "The second and third arguments to a select do not have a matching type:\n"
        << "  " << true_value << " has type " << true_value.type() << "\n"
        << "  " << false_value << " has type " << false_value.type() << "\n";

    return Internal::Select::make(condition, true_value, false_value);
}

/** A multi-way variant of select similar to a switch statement in C,
 * which can accept multiple conditions and values in pairs. Evaluates
 * to the first value for which the condition is true. Returns the
 * final value if all conditions are false. */
// @{
inline Expr select(Expr c1, Expr v1,
                   Expr c2, Expr v2,
                   Expr default_val) {
    return select(c1, v1,
                  select(c2, v2, default_val));
}
inline Expr select(Expr c1, Expr v1,
                   Expr c2, Expr v2,
                   Expr c3, Expr v3,
                   Expr default_val) {
    return select(c1, v1,
                  c2, v2,
                  select(c3, v3, default_val));
}
inline Expr select(Expr c1, Expr v1,
                   Expr c2, Expr v2,
                   Expr c3, Expr v3,
                   Expr c4, Expr v4,
                   Expr default_val) {
    return select(c1, v1,
                  c2, v2,
                  c3, v3,
                  select(c4, v4, default_val));
}
inline Expr select(Expr c1, Expr v1,
                   Expr c2, Expr v2,
                   Expr c3, Expr v3,
                   Expr c4, Expr v4,
                   Expr c5, Expr v5,
                   Expr default_val) {
    return select(c1, v1,
                  c2, v2,
                  c3, v3,
                  c4, v4,
                  select(c5, v5, default_val));
}
inline Expr select(Expr c1, Expr v1,
                   Expr c2, Expr v2,
                   Expr c3, Expr v3,
                   Expr c4, Expr v4,
                   Expr c5, Expr v5,
                   Expr c6, Expr v6,
                   Expr default_val) {
    return select(c1, v1,
                  c2, v2,
                  c3, v3,
                  c4, v4,
                  c5, v5,
                  select(c6, v6, default_val));
}
inline Expr select(Expr c1, Expr v1,
                   Expr c2, Expr v2,
                   Expr c3, Expr v3,
                   Expr c4, Expr v4,
                   Expr c5, Expr v5,
                   Expr c6, Expr v6,
                   Expr c7, Expr v7,
                   Expr default_val) {
    return select(c1, v1,
                  c2, v2,
                  c3, v3,
                  c4, v4,
                  c5, v5,
                  c6, v6,
                  select(c7, v7, default_val));
}
inline Expr select(Expr c1, Expr v1,
                   Expr c2, Expr v2,
                   Expr c3, Expr v3,
                   Expr c4, Expr v4,
                   Expr c5, Expr v5,
                   Expr c6, Expr v6,
                   Expr c7, Expr v7,
                   Expr c8, Expr v8,
                   Expr default_val) {
    return select(c1, v1,
                  c2, v2,
                  c3, v3,
                  c4, v4,
                  c5, v5,
                  c6, v6,
                  c7, v7,
                  select(c8, v8, default_val));
}
inline Expr select(Expr c1, Expr v1,
                   Expr c2, Expr v2,
                   Expr c3, Expr v3,
                   Expr c4, Expr v4,
                   Expr c5, Expr v5,
                   Expr c6, Expr v6,
                   Expr c7, Expr v7,
                   Expr c8, Expr v8,
                   Expr c9, Expr v9,
                   Expr default_val) {
    return select(c1, v1,
                  c2, v2,
                  c3, v3,
                  c4, v4,
                  c5, v5,
                  c6, v6,
                  c7, v7,
                  c8, v8,
                  select(c9, v9, default_val));
}
inline Expr select(Expr c1, Expr v1,
                   Expr c2, Expr v2,
                   Expr c3, Expr v3,
                   Expr c4, Expr v4,
                   Expr c5, Expr v5,
                   Expr c6, Expr v6,
                   Expr c7, Expr v7,
                   Expr c8, Expr v8,
                   Expr c9, Expr v9,
                   Expr c10, Expr v10,
                   Expr default_val) {
    return select(c1, v1,
                  c2, v2,
                  c3, v3,
                  c4, v4,
                  c5, v5,
                  c6, v6,
                  c7, v7,
                  c8, v8,
                  c9, v9,
                  select(c10, v10, default_val));
}
// @}

/** Return the sine of a floating-point expression. If the argument is
 * not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
inline Expr sin(Expr x) {
    user_assert(x.defined()) << "sin of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "sin_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "sin_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the arcsine of a floating-point expression. If the argument
 * is not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
inline Expr asin(Expr x) {
    user_assert(x.defined()) << "asin of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "asin_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "asin_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the cosine of a floating-point expression. If the argument
 * is not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
inline Expr cos(Expr x) {
    user_assert(x.defined()) << "cos of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "cos_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "cos_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the arccosine of a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). Does not
 * vectorize well. */
inline Expr acos(Expr x) {
    user_assert(x.defined()) << "acos of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "acos_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "acos_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the tangent of a floating-point expression. If the argument
 * is not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
inline Expr tan(Expr x) {
    user_assert(x.defined()) << "tan of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "tan_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "tan_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the arctangent of a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). Does not
 * vectorize well. */
inline Expr atan(Expr x) {
    user_assert(x.defined()) << "atan of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "atan_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "atan_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the angle of a floating-point gradient. If the argument is
 * not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
inline Expr atan2(Expr y, Expr x) {
    user_assert(x.defined() && y.defined()) << "atan2 of undefined Expr\n";

    if (y.type() == Float(64)) {
        x = cast<double>(x);
        return Internal::Call::make(Float(64), "atan2_f64", vec(y, x), Internal::Call::Extern);
    } else {
        y = cast<float>(y);
        x = cast<float>(x);
        return Internal::Call::make(Float(32), "atan2_f32", vec(y, x), Internal::Call::Extern);
    }
}

/** Return the hyperbolic sine of a floating-point expression.  If the
 *  argument is not floating-point, it is cast to Float(32). Does not
 *  vectorize well. */
inline Expr sinh(Expr x) {
    user_assert(x.defined()) << "sinh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "sinh_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "sinh_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the hyperbolic arcsinhe of a floating-point expression.  If
 * the argument is not floating-point, it is cast to Float(32). Does
 * not vectorize well. */
inline Expr asinh(Expr x) {
    user_assert(x.defined()) << "asinh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "asinh_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "asinh_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the hyperbolic cosine of a floating-point expression.  If
 * the argument is not floating-point, it is cast to Float(32). Does
 * not vectorize well. */
inline Expr cosh(Expr x) {
    user_assert(x.defined()) << "cosh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "cosh_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "cosh_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the hyperbolic arccosine of a floating-point expression.
 * If the argument is not floating-point, it is cast to
 * Float(32). Does not vectorize well. */
inline Expr acosh(Expr x) {
    user_assert(x.defined()) << "acosh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "acosh_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "acosh_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the hyperbolic tangent of a floating-point expression.  If
 * the argument is not floating-point, it is cast to Float(32). Does
 * not vectorize well. */
inline Expr tanh(Expr x) {
    user_assert(x.defined()) << "tanh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "tanh_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "tanh_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the hyperbolic arctangent of a floating-point expression.
 * If the argument is not floating-point, it is cast to
 * Float(32). Does not vectorize well. */
inline Expr atanh(Expr x) {
    user_assert(x.defined()) << "atanh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "atanh_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "atanh_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the square root of a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). Typically
 * vectorizes cleanly. */
inline Expr sqrt(Expr x) {
    user_assert(x.defined()) << "sqrt of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "sqrt_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "sqrt_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the square root of the sum of the squares of two
 * floating-point expressions. If the argument is not floating-point,
 * it is cast to Float(32). Vectorizes cleanly. */
inline Expr hypot(Expr x, Expr y) {
    return sqrt(x*x + y*y);
}

/** Return the exponential of a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). For
 * Float(64) arguments, this calls the system exp function, and does
 * not vectorize well. For Float(32) arguments, this function is
 * vectorizable, does the right thing for extremely small or extremely
 * large inputs, and is accurate up to the last bit of the
 * mantissa. Vectorizes cleanly. */
inline Expr exp(Expr x) {
    user_assert(x.defined()) << "exp of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "exp_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "exp_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the logarithm of a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). For
 * Float(64) arguments, this calls the system log function, and does
 * not vectorize well. For Float(32) arguments, this function is
 * vectorizable, does the right thing for inputs <= 0 (returns -inf or
 * nan), and is accurate up to the last bit of the
 * mantissa. Vectorizes cleanly. */
inline Expr log(Expr x) {
    user_assert(x.defined()) << "log of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "log_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "log_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return one floating point expression raised to the power of
 * another. The type of the result is given by the type of the first
 * argument. If the first argument is not a floating-point type, it is
 * cast to Float(32). For Float(32), cleanly vectorizable, and
 * accurate up to the last few bits of the mantissa. Gets worse when
 * approaching overflow. Vectorizes cleanly. */
inline Expr pow(Expr x, Expr y) {
    user_assert(x.defined() && y.defined()) << "pow of undefined Expr\n";

    if (const int *i = as_const_int(y)) {
        return raise_to_integer_power(x, *i);
    }

    if (x.type() == Float(64)) {
        y = cast<double>(y);
        return Internal::Call::make(Float(64), "pow_f64", vec(x, y), Internal::Call::Extern);
    } else {
        x = cast<float>(x);
        y = cast<float>(y);
        return Internal::Call::make(Float(32), "pow_f32", vec(x, y), Internal::Call::Extern);
    }
}

/** Evaluate the error function erf. Only available for
 * Float(32). Accurate up to the last three bits of the
 * mantissa. Vectorizes cleanly. */
inline Expr erf(Expr x) {
    user_assert(x.defined()) << "erf of undefined Expr\n";
    user_assert(x.type() == Float(32)) << "erf only takes float arguments\n";
    return Internal::halide_erf(x);
}

/** Fast approximate cleanly vectorizable log for Float(32). Returns
 * nonsense for x <= 0.0f. Accurate up to the last 5 bits of the
 * mantissa. Vectorizes cleanly. */
EXPORT Expr fast_log(Expr x);

/** Fast approximate cleanly vectorizable exp for Float(32). Returns
 * nonsense for inputs that would overflow or underflow. Typically
 * accurate up to the last 5 bits of the mantissa. Gets worse when
 * approaching overflow. Vectorizes cleanly. */
EXPORT Expr fast_exp(Expr x);

/** Fast approximate cleanly vectorizable pow for Float(32). Returns
 * nonsense for x < 0.0f. Accurate up to the last 5 bits of the
 * mantissa for typical exponents. Gets worse when approaching
 * overflow. Vectorizes cleanly. */
inline Expr fast_pow(Expr x, Expr y) {
    if (const int *i = as_const_int(y)) {
        return raise_to_integer_power(x, *i);
    }

    x = cast<float>(x);
    y = cast<float>(y);
    return select(x == 0.0f, 0.0f, fast_exp(fast_log(x) * y));
}

/** Fast approximate inverse for Float(32). Corresponds to the rcpps
 * instruction on x86, and the vrecpe instruction on ARM. Vectorizes
 * cleanly. */
inline Expr fast_inverse(Expr x) {
    user_assert(x.type() == Float(32)) << "fast_inverse only takes float arguments\n";
    return Internal::Call::make(x.type(), "fast_inverse_f32", vec(x), Internal::Call::Extern);
}

/** Fast approximate inverse square root for Float(32). Corresponds to
 * the rsqrtps instruction on x86, and the vrsqrte instruction on
 * ARM. Vectorizes cleanly. */
inline Expr fast_inverse_sqrt(Expr x) {
    user_assert(x.type() == Float(32)) << "fast_inverse_sqrt only takes float arguments\n";
    return Internal::Call::make(x.type(), "fast_inverse_sqrt_f32", vec(x), Internal::Call::Extern);
}

/** Return the greatest whole number less than or equal to a
 * floating-point expression. If the argument is not floating-point,
 * it is cast to Float(32). The return value is still in floating
 * point, despite being a whole number. Vectorizes cleanly. */
inline Expr floor(Expr x) {
    user_assert(x.defined()) << "floor of undefined Expr\n";
    if (x.type().element_of() == Float(64)) {
        return Internal::Call::make(x.type(), "floor_f64", vec(x), Internal::Call::Extern);
    } else {
        Type t = Float(32, x.type().width);
        return Internal::Call::make(t, "floor_f32", vec(cast(t, x)), Internal::Call::Extern);
    }
}

/** Return the least whole number greater than or equal to a
 * floating-point expression. If the argument is not floating-point,
 * it is cast to Float(32). The return value is still in floating
 * point, despite being a whole number. Vectorizes cleanly. */
inline Expr ceil(Expr x) {
    user_assert(x.defined()) << "ceil of undefined Expr\n";
    if (x.type().element_of() == Float(64)) {
        return Internal::Call::make(x.type(), "ceil_f64", vec(x), Internal::Call::Extern);
    } else {
        Type t = Float(32, x.type().width);
        return Internal::Call::make(t, "ceil_f32", vec(cast(t, x)), Internal::Call::Extern);
    }
}

/** Return the whole number closest to a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). The return value
 * is still in floating point, despite being a whole number. On ties, we
 * follow IEEE754 conventions and round to the nearest even number. Vectorizes
 * cleanly. */
inline Expr round(Expr x) {
    user_assert(x.defined()) << "round of undefined Expr\n";
    if (x.type().element_of() == Float(64)) {
        return Internal::Call::make(Float(64), "round_f64", vec(x), Internal::Call::Extern);
    } else {
        Type t = Float(32, x.type().width);
        return Internal::Call::make(t, "round_f32", vec(cast(t, x)), Internal::Call::Extern);
    }
}

/** Return the integer part of a floating-point expression. If the argument is
 * not floating-point, it is cast to Float(32). The return value is still in
 * floating point, despite being a whole number. Vectorizes cleanly. */
inline Expr trunc(Expr x) {
    user_assert(x.defined()) << "trunc of undefined Expr\n";
    if (x.type().element_of() == Float(64)) {
        return Internal::Call::make(Float(64), "trunc_f64", vec(x), Internal::Call::Extern);
    } else {
        Type t = Float(32, x.type().width);
        return Internal::Call::make(t, "trunc_f32", vec(cast(t, x)), Internal::Call::Extern);
    }
}

/** Returns true if the argument is a Not a Number (NaN). Requires a
  * floating point argument.  Vectorizes cleanly. */
inline Expr is_nan(Expr x) {
    user_assert(x.defined()) << "is_nan of undefined Expr\n";
    user_assert(x.type().is_float()) << "is_nan only works for float";
    Type t = Bool(x.type().width);
    if (x.type().element_of() == Float(64)) {
        return Internal::Call::make(t, "is_nan_f64", vec(x), Internal::Call::Extern);
    } else {
        Type ft = Float(32, x.type().width);
        return Internal::Call::make(t, "is_nan_f32", vec(cast(ft, x)), Internal::Call::Extern);
    }
}

/** Return the fractional part of a floating-point expression. If the argument
 *  is not floating-point, it is cast to Float(32). The return value has the
 *  same sign as the original expression. Vectorizes cleanly. */
inline Expr fract(Expr x) {
    user_assert(x.defined()) << "fract of undefined Expr\n";
    return x - trunc(x);
}

/** Reinterpret the bits of one value as another type. */
inline Expr reinterpret(Type t, Expr e) {
    user_assert(e.defined()) << "reinterpret of undefined Expr\n";
    int from_bits = e.type().bits * e.type().width;
    int to_bits = t.bits * t.width;
    user_assert(from_bits == to_bits)
        << "Reinterpret cast from type " << e.type()
        << " which has " << from_bits
        << " bits, to type " << t
        << " which has " << to_bits << " bits\n";
    return Internal::Call::make(t, Internal::Call::reinterpret, vec(e), Internal::Call::Intrinsic);
}

template<typename T>
inline Expr reinterpret(Expr e) {
    return reinterpret(type_of<T>(), e);
}

/** Return the bitwise and of two expressions (which need not have the
 * same type). The type of the result is the type of the first
 * argument. */
inline Expr operator&(Expr x, Expr y) {
    user_assert(x.defined() && y.defined()) << "bitwise and of undefined Expr\n";
    // First widen or narrow, then bitcast.
    if (y.type().bits != x.type().bits) {
        Type t = y.type();
        t.bits = x.type().bits;
        y = cast(t, y);
    }
    if (y.type() != x.type()) {
        y = reinterpret(x.type(), y);
    }
    return Internal::Call::make(x.type(), Internal::Call::bitwise_and, vec(x, y), Internal::Call::Intrinsic);
}

/** Return the bitwise or of two expressions (which need not have the
 * same type). The type of the result is the type of the first
 * argument. */
inline Expr operator|(Expr x, Expr y) {
    user_assert(x.defined() && y.defined()) << "bitwise or of undefined Expr\n";
    // First widen or narrow, then bitcast.
    if (y.type().bits != x.type().bits) {
        Type t = y.type();
        t.bits = x.type().bits;
        y = cast(t, y);
    }
    if (y.type() != x.type()) {
        y = reinterpret(x.type(), y);
    }
    return Internal::Call::make(x.type(), Internal::Call::bitwise_or, vec(x, y), Internal::Call::Intrinsic);
}

/** Return the bitwise exclusive or of two expressions (which need not
 * have the same type). The type of the result is the type of the
 * first argument. */
inline Expr operator^(Expr x, Expr y) {
    user_assert(x.defined() && y.defined()) << "bitwise or of undefined Expr\n";
    // First widen or narrow, then bitcast.
    if (y.type().bits != x.type().bits) {
        Type t = y.type();
        t.bits = x.type().bits;
        y = cast(t, y);
    }
    if (y.type() != x.type()) {
        y = reinterpret(x.type(), y);
    }
    return Internal::Call::make(x.type(), Internal::Call::bitwise_xor, vec(x, y), Internal::Call::Intrinsic);
}

/** Return the bitwise not of an expression. */
inline Expr operator~(Expr x) {
    user_assert(x.defined()) << "bitwise or of undefined Expr\n";
    return Internal::Call::make(x.type(), Internal::Call::bitwise_not, vec(x), Internal::Call::Intrinsic);
}

/** Shift the bits of an integer value left. This is actually less
 * efficient than multiplying by 2^n, because Halide's optimization
 * passes understand multiplication, and will compile it to
 * shifting. This operator is only for if you really really need bit
 * shifting (e.g. because the exponent is a run-time parameter). The
 * type of the result is equal to the type of the first argument. Both
 * arguments must have integer type. */
inline Expr operator<<(Expr x, Expr y) {
    user_assert(x.defined() && y.defined()) << "shift left of undefined Expr\n";
    user_assert(!x.type().is_float()) << "First argument to shift left is a float: " << x << "\n";
    user_assert(!y.type().is_float()) << "Second argument to shift left is a float: " << y << "\n";
    Internal::match_types(x, y);
    return Internal::Call::make(x.type(), Internal::Call::shift_left, vec(x, y), Internal::Call::Intrinsic);
}

/** Shift the bits of an integer value right. Does sign extension for
 * signed integers. This is less efficient than dividing by a power of
 * two. Halide's definition of division (always round to negative
 * infinity) means that all divisions by powers of two get compiled to
 * bit-shifting, and Halide's optimization routines understand
 * division and can work with it. The type of the result is equal to
 * the type of the first argument. Both arguments must have integer
 * type. */
inline Expr operator>>(Expr x, Expr y) {
    user_assert(x.defined() && y.defined()) << "shift right of undefined Expr\n";
    user_assert(!x.type().is_float()) << "First argument to shift right is a float: " << x << "\n";
    user_assert(!y.type().is_float()) << "Second argument to shift right is a float: " << y << "\n";
    Internal::match_types(x, y);
    return Internal::Call::make(x.type(), Internal::Call::shift_right, vec(x, y), Internal::Call::Intrinsic);
}

/** Linear interpolate between the two values according to a weight.
 * \param zero_val The result when weight is 0
 * \param one_val The result when weight is 1
 * \param weight The interpolation amount
 *
 * Both zero_val and one_val must have the same type. All types are
 * supported, including bool.
 *
 * The weight is treated as its own type and must be float or an
 * unsigned integer type. It is scaled to the bit-size of the type of
 * x and y if they are integer, or converted to float if they are
 * float. Integer weights are converted to float via division by the
 * full-range value of the weight's type. Floating-point weights used
 * to interpolate between integer values must be between 0.0f and
 * 1.0f, and an error may be signaled if it is not provably so. (clamp
 * operators can be added to provide proof. Currently an error is only
 * signalled for constant weights.)
 *
 * For integer linear interpolation, out of range values cannot be
 * represented. In particular, weights that are conceptually less than
 * 0 or greater than 1.0 are not representable. As such the result is
 * always between x and y (inclusive of course). For lerp with
 * floating-point values and floating-point weight, the full range of
 * a float is valid, however underflow and overflow can still occur.
 *
 * Ordering is not required between zero_val and one_val:
 *     lerp(42, 69, .5f) == lerp(69, 42, .5f) == 56
 *
 * Results for integer types are for exactly rounded arithmetic. As
 * such, there are cases where 16-bit and float differ because 32-bit
 * floating-point (float) does not have enough precision to produce
 * the exact result. (Likely true for 32-bit integer
 * vs. double-precision floating-point as well.)
 *
 * At present, double precision and 64-bit integers are not supported.
 *
 * Generally, lerp will vectorize as if it were an operation on a type
 * twice the bit size of the inferred type for x and y.
 *
 * Some examples:
 * \code
 *
 *     // Since Halide does not have direct type delcarations, casts
 *     // below are used to indicate the types of the parameters.
 *     // Such casts not required or expected in actual code where types
 *     // are inferred.
 *
 *     lerp(cast<float>(x), cast<float>(y), cast<float>(w)) ->
 *       x * (1.0f - w) + y * w
 *
 *     lerp(cast<uint8_t>(x), cast<uint8_t>(y), cast<uint8_t>(w)) ->
 *       cast<uint8_t>(cast<uint8_t>(x) * (1.0f - cast<uint8_t>(w) / 255.0f) +
 *                     cast<uint8_t>(y) * cast<uint8_t>(w) / 255.0f + .5f)
 *
 *     // Note addition in Halide promoted uint8_t + int8_t to int16_t already,
 *     // the outer cast is added for clarity.
 *     lerp(cast<uint8_t>(x), cast<int8_t>(y), cast<uint8_t>(w)) ->
 *       cast<int16_t>(cast<uint8_t>(x) * (1.0f - cast<uint8_t>(w) / 255.0f) +
 *                     cast<int8_t>(y) * cast<uint8_t>(w) / 255.0f + .5f)
 *
 *     lerp(cast<int8_t>(x), cast<int8_t>(y), cast<float>(w)) ->
 *       cast<int8_t>(cast<int8_t>(x) * (1.0f - cast<float>(w)) +
 *                    cast<int8_t>(y) * cast<uint8_t>(w))
 *
 * \endcode
 * */
inline Expr lerp(Expr zero_val, Expr one_val, Expr weight) {
    user_assert(zero_val.defined()) << "lerp with undefined zero value";
    user_assert(one_val.defined()) << "lerp with undefined one value";
    user_assert(weight.defined()) << "lerp with undefined weight";

    // We allow integer constants through, so that you can say things
    // like lerp(0, cast<uint8_t>(x), alpha) and produce an 8-bit
    // result. Note that lerp(0.0f, cast<uint8_t>(x), alpha) will
    // produce an error, as will lerp(0.0f, cast<double>(x),
    // alpha). lerp(0, cast<float>(x), alpha) is also allowed and will
    // produce a float result.
    if (as_const_int(zero_val)) {
        zero_val = cast(one_val.type(), zero_val);
    }
    if (as_const_int(one_val)) {
        one_val = cast(zero_val.type(), one_val);
    }

    user_assert(zero_val.type() == one_val.type())
        << "Can't lerp between " << zero_val << " of type " << zero_val.type()
        << " and " << one_val << " of different type " << one_val.type() << "\n";
    user_assert((weight.type().is_uint() || weight.type().is_float()))
        << "A lerp weight must be an unsigned integer or a float, but "
        << "lerp weight " << weight << " has type " << weight.type() << ".\n";
    user_assert((zero_val.type().is_float() || zero_val.type().width <= 32))
        << "Lerping between 64-bit integers is not supported\n";
    // Compilation error for constant weight that is out of range for integer use
    // as this seems like an easy to catch gotcha.
    if (!zero_val.type().is_float()) {
        const float *const_weight = as_const_float(weight);
        if (const_weight) {
            user_assert(*const_weight >= 0.0f && *const_weight <= 1.0f)
                << "Floating-point weight for lerp with integer arguments is "
                << *const_weight << ", which is not in the range [0.0f, 1.0f].\n";
        }
    }
    return Internal::Call::make(zero_val.type(), Internal::Call::lerp,
                                vec(zero_val, one_val, weight),
                                Internal::Call::Intrinsic);
}

/** Count the number of set bits in an expression. */
inline Expr popcount(Expr x) {
    user_assert(x.defined()) << "popcount of undefined Expr\n";
    return Internal::Call::make(x.type(), Internal::Call::popcount,
                                vec(x), Internal::Call::Intrinsic);
}

/** Count the number of leading zero bits in an expression. The result is
 *  undefined if the value of the expression is zero. */
inline Expr count_leading_zeros(Expr x) {
    user_assert(x.defined()) << "count leading zeros of undefined Expr\n";
    return Internal::Call::make(x.type(), Internal::Call::count_leading_zeros,
                                vec(x), Internal::Call::Intrinsic);
}

/** Count the number of trailing zero bits in an expression. The result is
 *  undefined if the value of the expression is zero. */
inline Expr count_trailing_zeros(Expr x) {
    user_assert(x.defined()) << "count trailing zeros of undefined Expr\n";
    return Internal::Call::make(x.type(), Internal::Call::count_trailing_zeros,
                                vec(x), Internal::Call::Intrinsic);
}

/** Return a random variable representing a uniformly distributed
 * float in the half-open interval [0.0f, 1.0f). For random numbers of
 * other types, use lerp with a random float as the last parameter.
 *
 * Optionally takes a seed.
 *
 * Note that:
 \code
 Expr x = random_float();
 Expr y = x + x;
 \endcode
 *
 * is very different to
 *
 \code
 Expr y = random_float() + random_float();
 \endcode
 *
 * The first doubles a random variable, and the second adds two
 * independent random variables.
 *
 * A given random variable takes on a unique value that depends
 * deterministically on the pure variables of the function they belong
 * to, the identity of the function itself, and which definition of
 * the function it is used in. They are, however, shared across tuple
 * elements.
 *
 * This function vectorizes cleanly.
 */
inline Expr random_float(Expr seed = Expr()) {
    // Random floats get even IDs
    static int counter = -2;
    counter += 2;

    std::vector<Expr> args;
    if (seed.defined()) {
        user_assert(seed.type() == Int(32))
            << "The seed passed to random_float must have type Int(32), but instead is "
            << seed << " of type " << seed.type() << "\n";
        args.push_back(seed);
    }
    args.push_back(counter);

    return Internal::Call::make(Float(32), Internal::Call::random,
                                args, Internal::Call::Intrinsic);
}

/** Return a random variable representing a uniformly distributed
 * 32-bit integer. See \ref random_float. Vectorizes cleanly. */
inline Expr random_int(Expr seed = Expr()) {
    // Random ints get odd IDs
    static int counter = -1;
    counter += 2;

    std::vector<Expr> args;
    if (seed.defined()) {
        user_assert(seed.type() == Int(32))
            << "The seed passed to random_int must have type Int(32), but instead is "
            << seed << " of type " << seed.type() << "\n";
        args.push_back(seed);
    }
    args.push_back(counter);

    return Internal::Call::make(Int(32), Internal::Call::random,
                                args, Internal::Call::Intrinsic);
}

// For the purposes of a call to print, const char * can convert
// silently to an Expr
struct PrintArg {
    Expr expr;
    PrintArg(const char *str) : expr(std::string(str)) {}
    template<typename T> PrintArg(T e) : expr(e) {}
    operator Expr() {return expr;}
};

/** Create an Expr that prints out its value whenever it is
 * evaluated. It also prints out everything else in the arguments
 * list, separated by spaces. This can include string literals. */
// @{
EXPORT Expr print(const std::vector<Expr> &values);
inline Expr print(PrintArg a) {
    return print(Internal::vec<Expr>(a));
}
inline Expr print(PrintArg a, PrintArg b) {
    return print(Internal::vec<Expr>(a, b));
}
inline Expr print(PrintArg a, PrintArg b, PrintArg c) {
    return print(Internal::vec<Expr>(a, b, c));
}
inline Expr print(PrintArg a, PrintArg b, PrintArg c, PrintArg d) {
    return print(Internal::vec<Expr>(a, b, c, d));
}
inline Expr print(PrintArg a, PrintArg b, PrintArg c, PrintArg d, PrintArg e) {
    return print(Internal::vec<Expr>(a, b, c, d, e));
}
inline Expr print(PrintArg a, PrintArg b, PrintArg c, PrintArg d, PrintArg e, PrintArg f) {
    return print(Internal::vec<Expr>(a, b, c, d, e, f));
}
inline Expr print(PrintArg a, PrintArg b, PrintArg c, PrintArg d, PrintArg e, PrintArg f, PrintArg g) {
    return print(Internal::vec<Expr>(a, b, c, d, e, f, g));
}
inline Expr print(PrintArg a, PrintArg b, PrintArg c, PrintArg d, PrintArg e, PrintArg f, PrintArg g, PrintArg h) {
    return print(Internal::vec<Expr>(a, b, c, d, e, f, g, h));
}
// @}

/** Create an Expr that prints whenever it is evaluated, provided that
 * the condition is true. */
// @{
EXPORT Expr print_when(Expr condition, const std::vector<Expr> &values);
inline Expr print_when(Expr condition, PrintArg a) {
    return print_when(condition, Internal::vec<Expr>(a));
}
inline Expr print_when(Expr condition, PrintArg a, PrintArg b) {
    return print_when(condition, Internal::vec<Expr>(a, b));
}
inline Expr print_when(Expr condition, PrintArg a, PrintArg b, PrintArg c) {
    return print_when(condition, Internal::vec<Expr>(a, b, c));
}
inline Expr print_when(Expr condition, PrintArg a, PrintArg b, PrintArg c, PrintArg d) {
    return print_when(condition, Internal::vec<Expr>(a, b, c, d));
}
inline Expr print_when(Expr condition, PrintArg a, PrintArg b, PrintArg c, PrintArg d, PrintArg e) {
    return print_when(condition, Internal::vec<Expr>(a, b, c, d, e));
}
inline Expr print_when(Expr condition, PrintArg a, PrintArg b, PrintArg c, PrintArg d, PrintArg e, PrintArg f) {
    return print_when(condition, Internal::vec<Expr>(a, b, c, d, e, f));
}
inline Expr print_when(Expr condition, PrintArg a, PrintArg b, PrintArg c, PrintArg d, PrintArg e, PrintArg f, PrintArg g) {
    return print_when(condition, Internal::vec<Expr>(a, b, c, d, e, f, g));
}
inline Expr print_when(Expr condition, PrintArg a, PrintArg b, PrintArg c, PrintArg d, PrintArg e, PrintArg f, PrintArg g, PrintArg h) {
    return print_when(condition, Internal::vec<Expr>(a, b, c, d, e, f, g, h));
}
// @}


/** Return an undef value of the given type. Halide skips stores that
 * depend on undef values, so you can use this to mean "do not modify
 * this memory location". This is an escape hatch that can be used for
 * several things:
 *
 * You can define a reduction with no pure step, by setting the pure
 * step to undef. Do this only if you're confident that the update
 * steps are sufficient to correctly fill in the domain.
 *
 * For a tuple-valued reduction, you can write an update step that
 * only updates some tuple elements.
 *
 * You can define single-stage pipeline that only has update steps,
 * and depends on the values already in the output buffer.
 *
 * Use this feature with great caution, as you can use it to load from
 * uninitialized memory.
 */
inline Expr undef(Type t) {
    return Internal::Call::make(t, Internal::Call::undef,
                                std::vector<Expr>(),
                                Internal::Call::Intrinsic);
}

template<typename T>
inline Expr undef() {
    return undef(type_of<T>());
}

/** Control the values used in the memoization cache key for memoize.
 * Normally parameters and other external dependencies are
 * automatically inferred and added to the cache key. The memoize_tag
 * operator allows computing one expression and using either the
 * computed value, or one or more other expressions in the cache key
 * instead of the parameter dependencies of the computation. The
 * single argument version is completely safe in that the cache key
 * will use the actual computed value -- it is difficult or imposible
 * to produce erroneous caching this way. The more-than-one argument
 * version allows generating cache keys that do not uniquely identify
 * the computation and thus can result in caching errors.
 *
 * A potential use for the single argument version is to handle a
 * floating-point parameter that is quantized to a small
 * integer. Mutliple values of the float will produce the same integer
 * and moving the caching to using the integer for the key is more
 * efficient.
 *
 * The main use for the more-than-one argument version is to provide
 * cache key information for Handles and ImageParams, which otherwise
 * are not allowed inside compute_cached operations. E.g. when passing
 * a group of parameters to an external array function via a Handle,
 * memoize_tag can be used to isolate the actual values used by that
 * computation. If an ImageParam is a constant image with a persistent
 * digest, memoize_tag can be used to key computations using that image
 * on the digest. */
// @{
EXPORT Expr memoize_tag(Expr result, const std::vector<Expr> &cache_key_values);
inline Expr memoize_tag(Expr result) {
    return memoize_tag(result, std::vector<Expr>());
}
inline Expr memoize_tag(Expr result, Expr a) {
    return memoize_tag(result, Internal::vec<Expr>(a));
}
inline Expr memoize_tag(Expr result, Expr a, Expr b) {
    return memoize_tag(result, Internal::vec<Expr>(a, b));
}
inline Expr memoize_tag(Expr result, Expr a, Expr b, Expr c) {
    return memoize_tag(result, Internal::vec<Expr>(a, b, c));
}
inline Expr memoize_tag(Expr result, Expr a, Expr b, Expr c, Expr d) {
    return memoize_tag(result, Internal::vec<Expr>(a, b, c, d));
}
inline Expr memoize_tag(Expr result, Expr a, Expr b, Expr c, Expr d, Expr e) {
    return memoize_tag(result, Internal::vec<Expr>(a, b, c, d, e));
}
inline Expr memoize_tag(Expr result, Expr a, Expr b, Expr c, Expr d, Expr e, Expr f) {
    return memoize_tag(result, Internal::vec<Expr>(a, b, c, d, e, f));
}
inline Expr memoize_tag(Expr result, Expr a, Expr b, Expr c, Expr d, Expr e, Expr f, Expr g) {
    return memoize_tag(result, Internal::vec<Expr>(a, b, c, d, e, f, g));
}
inline Expr memoize_tag(Expr result, Expr a, Expr b, Expr c, Expr d, Expr e, Expr f, Expr g, Expr h) {
    return memoize_tag(result, Internal::vec<Expr>(a, b, c, d, e, f, g, h));
}
// @}

/** Expressions tagged with this intrinsic are considered to be part
 * of the steady state of some loop with a nasty beginning and end
 * (e.g. a boundary condition). When Halide encounters likely
 * intrinsics, it splits the containing loop body into three, and
 * tries to simplify down all conditions that lead to the likely. For
 * example, given the expression: select(x < 1, bar, x > 10, bar,
 * likely(foo)), Halide will split the loop over x into portions where
 * x < 1, 1 <= x <= 10, and x > 10.
 *
 * You're unlikely to want to call this directly. You probably want to
 * use the boundary condition helpers in the BoundaryConditon
 * namespace instead.
 */
inline Expr likely(Expr e) {
    return Internal::Call::make(e.type(), Internal::Call::likely,
                                Internal::vec<Expr>(e), Internal::Call::Intrinsic);
}

namespace Internal {

/** Given a scalar constant, return an Expr that represents it. This will
 * usually be a simple IntImm or FloatImm, with the exception of 64-bit values,
 * which are stored as wrappers to simple Call expressions.
 *
 * Note that in all cases, Expr.type == type_of<T>().
 */
template<typename T>
inline Expr scalar_to_constant_expr(T value) {
    // All integral types <= 32 bits, including bool
    return cast(type_of<T>(), Expr(static_cast<int32_t>(value)));
}

template<>
inline Expr scalar_to_constant_expr(float f32) {
    // float32 needs to skip the cast to int32
    return cast(Float(32), Expr(f32));
}

template<>
inline Expr scalar_to_constant_expr(double f64) {
    union {
        int32_t as_int32[2];
        double as_double;
    } u;
    u.as_double = f64;
    return Call::make(Float(64), Call::make_float64, vec(Expr(u.as_int32[0]), Expr(u.as_int32[1])), Call::Intrinsic);
}

template<>
inline Expr scalar_to_constant_expr(int64_t i) {
    const int32_t hi = static_cast<int32_t>(i >> 32);
    const int32_t lo = static_cast<int32_t>(i);
    return Call::make(Int(64), Call::make_int64, vec(Expr(hi), Expr(lo)), Call::Intrinsic);
}

template<>
inline Expr scalar_to_constant_expr(uint64_t u) {
    return cast(UInt(64), scalar_to_constant_expr<int64_t>(static_cast<int64_t>(u)));
}

namespace {

// extract_immediate is a private utility for scalar_from_constant_expr,
// and should not be used elsewhere
template<typename T>
inline bool extract_immediate(Expr e, T *value) {
    if (const IntImm* i = e.as<IntImm>()) {
        *value = static_cast<T>(i->value);
        return true;
    }
    return false;
}

template<>
inline bool extract_immediate(Expr e, float *value) {
    if (const FloatImm* f = e.as<FloatImm>()) {
        *value = static_cast<float>(f->value);
        return true;
    }
    if (const IntImm *i = e.as<IntImm>()) {
        *value = static_cast<float>(i->value);
        return true;
    }
    return false;
}

// We expect a float64-immediate to be either a call to make_float64()
// (with two IntImm), or a single FloatImm (if the value fits into a float32)
template<>
inline bool extract_immediate(Expr e, double *value) {
    union {
        int32_t as_int32[2];
        double as_double;
    } u;
    if (const Call* call = e.as<Call>()) {
        if (call->name == Call::make_float64) {
            if (!extract_immediate(call->args[0], &u.as_int32[0]) ||
                !extract_immediate(call->args[1], &u.as_int32[1])) {
                return false;
            }
            *value = u.as_double;
            return true;
        }
        return false;
    }
    if (const IntImm *i = e.as<IntImm>()) {
        *value = static_cast<double>(i->value);
        return true;
    }
    float f0;
    if (extract_immediate(e, &f0)) {
        *value = static_cast<double>(f0);
        return true;
    }
    return false;
}


// We expect an int64-immediate to be either a call to make_int64()
// (with two IntImm), or a single IntImm (if the value fits into an int32)
template<>
inline bool extract_immediate(Expr e, int64_t *value) {
    int32_t lo, hi;
    if (const Call* call = e.as<Call>()) {
        if (call->name == Call::make_int64) {
            if (!extract_immediate(call->args[0], &hi) ||
                !extract_immediate(call->args[1], &lo)) {
                return false;
            }
            *value = (static_cast<int64_t>(hi) << 32) | static_cast<uint32_t>(lo);
            return true;
        }
        return false;
    }
    if (extract_immediate(e, &lo)) {
        *value = static_cast<int64_t>(lo);
        return true;
    }
    return false;
}

template<>
inline bool extract_immediate(Expr e, uint64_t *value) {
    return extract_immediate(e, reinterpret_cast<int64_t*>(value));
}

}  // namespace

/** Given an Expr produced by scalar_to_constant_expr<T>, extract the constant value
 * of type T and return true. If the constant value cannot be converted to type
 * T, return false.
 *
 * In general, ScalarFromExpr<T>(ScalarToExpr<T>(v)) -> (v, true) for all scalar
 * type T, with the notable exception of T == float64, which will return true
 * but possibly lose precision.
 *
 * This function exists primarily to allow for code that needs to extract
 * the default/min/max values in a Parameter (e.g. to write metadata for
 * a compiled Generator); it is not intended to be a general Expr evaluator,
 * and should not be used as one.
 */
template<typename T>
inline bool scalar_from_constant_expr(Expr e, T *value) {
    if (!e.defined() || e.type() != type_of<T>()) {
        return false;
    }
    if (const Cast* c = e.as<Cast>()) {
        e = c->value;
    }
    return extract_immediate<T>(e, value);
}

}  // namespace Internal

}

#endif

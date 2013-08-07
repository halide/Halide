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
/** Is the expression either an IntImm, a FloatImm, or a Cast of the
 * same, or a Ramp or Broadcast of the same. Doesn't do any constant
 * folding. */
bool EXPORT is_const(Expr e);

/** Is the expression an IntImm, FloatImm of a particular value, or a
 * Cast, or Broadcast of the same. */
bool EXPORT is_const(Expr e, int v);

/** If an expression is an IntImm, return a pointer to its
 * value. Otherwise returns NULL. */
const int * EXPORT as_const_int(Expr e);

/** If an expression is a FloatImm, return a pointer to its
 * value. Otherwise returns NULL. */
const float * EXPORT as_const_float(Expr e);

/** Is the expression a constant integer power of two. Also returns
 * log base two of the expression if it is. */
bool EXPORT is_const_power_of_two(Expr e, int *bits);

/** Is the expression a const (as defined by is_const), and also
 * strictly greater than zero (in all lanes, if a vector expression) */
bool EXPORT is_positive_const(Expr e);

/** Is the expression a const (as defined by is_const), and also
 * strictly less than zero (in all lanes, if a vector expression) */
bool EXPORT is_negative_const(Expr e);

/** Is the expression a const (as defined by is_const), and also equal
 * to zero (in all lanes, if a vector expression) */
bool EXPORT is_zero(Expr e);

/** Is the expression a const (as defined by is_const), and also equal
 * to one (in all lanes, if a vector expression) */
bool EXPORT is_one(Expr e);

/** Is the expression a const (as defined by is_const), and also equal
 * to two (in all lanes, if a vector expression) */
bool EXPORT is_two(Expr e);

/** Given an integer value, cast it into a designated integer type
 * and return the bits as int. Unsigned types are returned as bits in the int
 * and should be cast to unsigned int for comparison.
 * int_cast_constant implements bit manipulations to wrap val into the
 * value range of the Type t.
 * For example, int_cast_constant(UInt(16), -1) returns 65535
 * int_cast_constant(Int(8), 128) returns -128
 */
int EXPORT int_cast_constant(Type t, int val);

/** Construct a const of the given type */
Expr EXPORT make_const(Type t, int val);

/** Construct a boolean constant from a C++ boolean value.
 * May also be a vector if width is given.
 * It is not possible to coerce a C++ boolean to Expr because
 * if we provide such a path then char objects can ambiguously
 * be converted to Halide Expr or to std::string.  The problem
 * is that C++ does not have a real bool type - it is in fact
 * close enough to char that C++ does not know how to distinguish them.
 * make_bool is the explicit coercion. */
Expr make_bool(bool val, int width = 1);

/** Construct the representation of zero in the given type */
Expr EXPORT make_zero(Type t);

/** Construct the representation of one in the given type */
Expr EXPORT make_one(Type t);

/** Construct the representation of two in the given type */
Expr EXPORT make_two(Type t);

/** Construct the constant boolean true. May also be a vector of
 * trues, if a width argument is given. */
Expr EXPORT const_true(int width = 1);

/** Construct the constant boolean false. May also be a vector of
 * falses, if a width argument is given. */
Expr EXPORT const_false(int width = 1);

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
void EXPORT match_types(Expr &a, Expr &b);

/** Halide's vectorizable transcendentals. */
// @{
Expr EXPORT halide_log(Expr a);
Expr EXPORT halide_exp(Expr a);
// @}

/** Raise an expression to an integer power by repeatedly multiplying
 * it by itself. */
Expr EXPORT raise_to_integer_power(Expr a, int b);


}

/** Cast an expression to the halide type corresponding to the C++ type T. */
template<typename T>
inline Expr cast(Expr a) {
    return cast(type_of<T>(), a);
}

/** Cast an expression to a new type. */
inline Expr cast(Type t, Expr a) {
    assert(a.defined() && "cast of undefined");
    if (a.type() == t) return a;
    if (t.is_vector()) {
        if (a.type().is_scalar()) {
            return Internal::Broadcast::make(cast(t.element_of(), a), t.width);
        } else if (const Internal::Broadcast *b = a.as<Internal::Broadcast>()) {
            assert(b->width == t.width);
            return Internal::Broadcast::make(cast(t.element_of(), b->value), t.width);
        }
    }
    return Internal::Cast::make(t, a);
}

/** Return the sum of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator+(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator+ of undefined");
    Internal::match_types(a, b);
    return Internal::Add::make(a, b);
}

/** Modify the first expression to be the sum of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator+=(Expr &a, Expr b) {
    assert(a.defined() && b.defined() && "operator+= of undefined");
    a = Internal::Add::make(a, cast(a.type(), b));
    return a;
}

/** Return the difference of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator-(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator- of undefined");
    Internal::match_types(a, b);
    return Internal::Sub::make(a, b);
}

/** Return the negative of the argument. Does no type casting, so more
 * formally: return that number which when added to the original,
 * yields zero of the same type. For unsigned integers the negative is
 * still an unsigned integer. E.g. in UInt(8), the negative of 56 is
 * 200, because 56 + 200 == 0 */
inline Expr operator-(Expr a) {
    assert(a.defined() && "operator- of undefined");
    return Internal::Sub::make(Internal::make_zero(a.type()), a);
}

/** Modify the first expression to be the difference of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator-=(Expr &a, Expr b) {
    assert(a.defined() && b.defined() && "operator-= of undefined");
    a = Internal::Sub::make(a, cast(a.type(), b));
    return a;
}

/** Return the product of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator*(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator* of undefined");
    Internal::match_types(a, b);
    return Internal::Mul::make(a, b);
}

/** Modify the first expression to be the product of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator*=(Expr &a, Expr b) {
    assert(a.defined() && b.defined() && "operator*= of undefined");
    a = Internal::Mul::make(a, cast(a.type(), b));
    return a;
}

/** Return the ratio of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator/(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator/ of undefined");
    Internal::match_types(a, b);
    return Internal::Div::make(a, b);
}

/** Modify the first expression to be the ratio of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator/=(Expr &a, Expr b) {
    assert(a.defined() && b.defined() && "operator/= of undefined");
    a = Internal::Div::make(a, cast(a.type(), b));
    return a;
}

/** Return the first argument reduced modulo the second, doing any
 * necessary type coercion using \ref Internal::match_types */
inline Expr operator%(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator% of undefined");
    Internal::match_types(a, b);
    return Internal::Mod::make(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is greater than the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator>(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator> of undefined");
    Internal::match_types(a, b);
    return Internal::GT::make(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is less than the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator<(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator< of undefined");
    Internal::match_types(a, b);
    return Internal::LT::make(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is less than or equal to the second, after doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator<=(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator<= of undefined");
    Internal::match_types(a, b);
    return Internal::LE::make(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is greater than or equal to the second, after doing any necessary
 * type coercion using \ref Internal::match_types */

inline Expr operator>=(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator>= of undefined");
    Internal::match_types(a, b);
    return Internal::GE::make(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is equal to the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator==(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator== of undefined");
    Internal::match_types(a, b);
    return Internal::EQ::make(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is not equal to the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator!=(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator!= of undefined");
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
    assert(a.defined() && b.defined() && "max of undefined");
    Internal::match_types(a, b);
    return Internal::Max::make(a, b);
}

/** Returns an expression representing the lesser of the two
 * arguments, after doing any necessary type coercion using
 * \ref Internal::match_types. Vectorizes cleanly on most platforms
 * (with the exception of integer types on x86 without SSE4). */
inline Expr min(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "min of undefined");
    Internal::match_types(a, b);
    return Internal::Min::make(a, b);
}

/** Clamps an expression to lie within the given bounds. The bounds
 * are type-cast to match the expression. Vectorizes as well as min/max. */
inline Expr clamp(Expr a, Expr min_val, Expr max_val) {
    assert(a.defined() && min_val.defined() && max_val.defined() &&
           "clamp of undefined");
    min_val = cast(a.type(), min_val);
    max_val = cast(a.type(), max_val);
    return Internal::Max::make(Internal::Min::make(a, max_val), min_val);
}

/** Returns the absolute value of a signed integer or floating-point
 * expression. Vectorizes cleanly. */
inline Expr abs(Expr a) {
    assert(a.defined() && "abs of undefined");
    if (a.type() == Int(8))
        return Internal::Call::make(Int(8), "abs_i8", vec(a), Internal::Call::Extern);
    if (a.type() == Int(16))
        return Internal::Call::make(Int(16), "abs_i16", vec(a), Internal::Call::Extern);
    if (a.type() == Int(32))
        return Internal::Call::make(Int(32), "abs_i32", vec(a), Internal::Call::Extern);
    if (a.type() == Int(64))
        return Internal::Call::make(Int(64), "abs_i64", vec(a), Internal::Call::Extern);
    if (a.type() == Float(32))
        return Internal::Call::make(Float(32), "abs_f32", vec(a), Internal::Call::Extern);
    if (a.type() == Float(64))
        return Internal::Call::make(Float(64), "abs_f64", vec(a), Internal::Call::Extern);
    assert(false && "Invalid type for abs");
    return 0; // prevent "control reaches end of non-void function" error
}

/** Returns an expression equivalent to the ternary operator in C. If
 * the first argument is true, then return the second, else return the
 * third. Typically vectorizes cleanly, but benefits from SSE41 or newer
 * on x86. */
inline Expr select(Expr a, Expr b, Expr c) {
    return Internal::Select::make(a, b, c);
}

/** Return the sine of a floating-point expression. If the argument is
 * not floating-point, is it cast to Float(32). Does not vectorize
 * well. */
inline Expr sin(Expr x) {
    assert(x.defined() && "sin of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "sin_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "sin_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the arcsine of a floating-point expression. If the argument
 * is not floating-point, is it cast to Float(32). Does not vectorize
 * well. */
inline Expr asin(Expr x) {
    assert(x.defined() && "asin of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "asin_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "asin_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the cosine of a floating-point expression. If the argument
 * is not floating-point, is it cast to Float(32). Does not vectorize
 * well. */
inline Expr cos(Expr x) {
    assert(x.defined() && "cos of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "cos_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "cos_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the arccosine of a floating-point expression. If the
 * argument is not floating-point, is it cast to Float(32). Does not
 * vectorize well. */
inline Expr acos(Expr x) {
    assert(x.defined() && "acos of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "acos_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "acos_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the tangent of a floating-point expression. If the argument
 * is not floating-point, is it cast to Float(32). Does not vectorize
 * well. */
inline Expr tan(Expr x) {
    assert(x.defined() && "tan of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "tan_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "tan_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the arctangent of a floating-point expression. If the
 * argument is not floating-point, is it cast to Float(32). Does not
 * vectorize well. */
inline Expr atan(Expr x) {
    assert(x.defined() && "atan of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "atan_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "atan_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the hyperbolic sine of a floating-point expression.  If the
 *  argument is not floating-point, is it cast to Float(32). Does not
 *  vectorize well. */
inline Expr sinh(Expr x) {
    assert(x.defined() && "sinh of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "sinh_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "sinh_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the hyperbolic arcsinhe of a floating-point expression.  If
 * the argument is not floating-point, is it cast to Float(32). Does
 * not vectorize well. */
inline Expr asinh(Expr x) {
    assert(x.defined() && "asinh of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "asinh_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "asinh_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the hyperbolic cosine of a floating-point expression.  If
 * the argument is not floating-point, is it cast to Float(32). Does
 * not vectorize well. */
inline Expr cosh(Expr x) {
    assert(x.defined() && "cosh of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "cosh_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "cosh_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the hyperbolic arccosine of a floating-point expression.
 * If the argument is not floating-point, is it cast to
 * Float(32). Does not vectorize well. */
inline Expr acosh(Expr x) {
    assert(x.defined() && "acosh of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "acosh_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "acosh_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the hyperbolic tangent of a floating-point expression.  If
 * the argument is not floating-point, is it cast to Float(32). Does
 * not vectorize well. */
inline Expr tanh(Expr x) {
    assert(x.defined() && "tanh of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "tanh_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "tanh_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the hyperbolic arctangent of a floating-point expression.
 * If the argument is not floating-point, is it cast to
 * Float(32). Does not vectorize well. */
inline Expr atanh(Expr x) {
    assert(x.defined() && "atanh of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "atanh_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "atanh_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the square root of a floating-point expression. If the
 * argument is not floating-point, is it cast to Float(32). Typically
 * vectorizes cleanly. */
inline Expr sqrt(Expr x) {
    assert(x.defined() && "sqrt of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "sqrt_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "sqrt_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the square root of the sum of the squares of two
 * floating-point expressions. If the argument is not floating-point,
 * is it cast to Float(32). Vectorizes cleanly. */
inline Expr hypot(Expr x, Expr y) {
    return sqrt(x*x + y*y);
}

/** Return the exponential of a floating-point expression. If the
 * argument is not floating-point, is it cast to Float(32). For
 * Float(64) arguments, this calls the system exp function, and does
 * not vectorize well. For Float(32) arguments, this function is
 * vectorizable, does the right thing for extremely small or extremely
 * large inputs, and is accurate up to the last bit of the
 * mantissa. Vectorizes cleanly. */
inline Expr exp(Expr x) {
    assert(x.defined() && "exp of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "exp_f64", vec(x), Internal::Call::Extern);
    } else {
        // return Internal::Call::make(Float(32), "exp_f32", vec(cast<float>(x)), Internal::Call::Extern);
        return Internal::halide_exp(cast<float>(x));
    }
}

/** Return the logarithm of a floating-point expression. If the
 * argument is not floating-point, is it cast to Float(32). For
 * Float(64) arguments, this calls the system log function, and does
 * not vectorize well. For Float(32) arguments, this function is
 * vectorizable, does the right thing for inputs <= 0 (returns -inf or
 * nan), and is accurate up to the last bit of the
 * mantissa. Vectorizes cleanly. */
inline Expr log(Expr x) {
    assert(x.defined() && "log of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "log_f64", vec(x), Internal::Call::Extern);
    } else {
        // return Internal::Call::make(Float(32), "log_f32", vec(cast<float>(x)), Internal::Call::Extern);
        return Internal::halide_log(cast<float>(x));
    }
}

/** Return one floating point expression raised to the power of
 * another. The type of the result is given by the type of the first
 * argument. If the first argument is not a floating-point type, it is
 * cast to Float(32). For Float(32), cleanly vectorizable, and
 * accurate up to the last few bits of the mantissa. Gets worse when
 * approaching overflow. Vectorizes cleanly. */
inline Expr pow(Expr x, Expr y) {
    assert(x.defined() && y.defined() && "pow of undefined");

    if (const int *i = as_const_int(y)) {
        return raise_to_integer_power(x, *i);
    }

    if (x.type() == Float(64)) {
        y = cast<double>(y);
        return Internal::Call::make(Float(64), "pow_f64", vec(x, y), Internal::Call::Extern);
    } else {
        x = cast<float>(x);
        y = cast<float>(y);
        return Internal::halide_exp(Internal::halide_log(x) * y);
    }
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

/** Return the greatest whole number less than or equal to a
 * floating-point expression. If the argument is not floating-point,
 * is it cast to Float(32). The return value is still in floating
 * point, despite being a whole number. Vectorizes cleanly. */
inline Expr floor(Expr x) {
    assert(x.defined() && "floor of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "floor_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "floor_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the least whole number greater than or equal to a
 * floating-point expression. If the argument is not floating-point,
 * is it cast to Float(32). The return value is still in floating
 * point, despite being a whole number. Vectorizes cleanly. */
inline Expr ceil(Expr x) {
    assert(x.defined() && "ceil of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "ceil_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "ceil_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Return the whole number closest to a floating-point expression. If
 * the argument is not floating-point, is it cast to Float(32). The
 * return value is still in floating point, despite being a whole
 * number. On ties, we round up. Vectorizes cleanly. */
inline Expr round(Expr x) {
    assert(x.defined() && "round of undefined");
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "round_f64", vec(x), Internal::Call::Extern);
    } else {
        return Internal::Call::make(Float(32), "round_f32", vec(cast<float>(x)), Internal::Call::Extern);
    }
}

/** Reinterpret the bits of one value as another type. */
inline Expr reinterpret(Type t, Expr e) {
    assert(e.defined() && "reinterpret of undefined");
    assert((t.bits * t.width) == (e.type().bits * e.type().width));
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
    assert(x.defined() && y.defined() && "bitwise and of undefined");
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
    assert(x.defined() && y.defined() && "bitwise or of undefined");
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
    assert(x.defined() && y.defined() && "bitwise or of undefined");
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
    assert(x.defined() && "bitwise or of undefined");
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
    assert(x.defined() && y.defined() && "shift left of undefined");
    assert(!x.type().is_float() && "First argument to shift left is a float.");
    assert(!y.type().is_float() && "Second argument to shift left is a float.");
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
    assert(x.defined() && y.defined() && "shift right of undefined");
    assert(!x.type().is_float() && "First argument to shift right is a float.");
    assert(!y.type().is_float() && "Second argument to shift right is a float.");
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
    assert(zero_val.defined() && "lerp with undefined zero value");
    assert(one_val.defined()  && "lerp with undefined one value");
    assert(weight.defined()   && "lerp with undefined weight");

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

    assert(zero_val.type() == one_val.type() &&
           "lerp zero and one values must be the same type.");
    assert((weight.type().is_uint() || weight.type().is_float()) &&
           "lerp weight must be unsigned or float.");
    assert((zero_val.type().is_float() || zero_val.type().width <= 32) &&
           "lerp with 64-bit integers is not supported.");
    // Compilation error for constant weight that is out of range for integer use
    // as this seems like an easy to catch gotcha.
    if (!zero_val.type().is_float()) {
        const float *const_weight = as_const_float(weight);
        if (const_weight != NULL) {
            assert(*const_weight >= 0.0f && *const_weight <= 1.0f &&
                   "floating-point weight for lerp with integer arguments must be between 0.0f and 1.0f.");
        }
    }
    return Internal::Call::make(zero_val.type(), Internal::Call::lerp,
                                vec(zero_val, one_val, weight),
                                Internal::Call::Intrinsic);
}

}


#endif

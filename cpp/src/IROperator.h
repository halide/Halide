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

/** Construct a const of the given type */
Expr EXPORT make_const(Type t, int val);

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
}

/** Cast an expression to the halide type corresponding to the C++ type T */
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
            return new Internal::Broadcast(cast(t.element_of(), a), t.width);
        } else if (const Internal::Broadcast *b = a.as<Internal::Broadcast>()) {
            assert(b->width == t.width);
            return new Internal::Broadcast(cast(t.element_of(), b->value), t.width);
        }
    }
    return new Internal::Cast(t, a);
}

/** Return the sum of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator+(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator+ of undefined");
    Internal::match_types(a, b);
    return new Internal::Add(a, b);
}
    
/** Modify the first expression to be the sum of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator+=(Expr &a, Expr b) {
    assert(a.defined() && b.defined() && "operator+= of undefined");
    a = new Internal::Add(a, cast(a.type(), b));
    return a;
}

/** Return the difference of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator-(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator- of undefined");
    Internal::match_types(a, b);
    return new Internal::Sub(a, b);
}

/** Return the negative of the argument. Does no type casting, so more
 * formally: return that number which when added to the original,
 * yields zero of the same type. For unsigned integers the negative is
 * still an unsigned integer. E.g. in UInt(8), the negative of 56 is
 * 200, because 56 + 200 == 0 */
inline Expr operator-(Expr a) {
    assert(a.defined() && "operator- of undefined");
    return new Internal::Sub(Internal::make_zero(a.type()), a);
}

/** Modify the first expression to be the difference of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator-=(Expr &a, Expr b) {
    assert(a.defined() && b.defined() && "operator-= of undefined");
    a = new Internal::Sub(a, cast(a.type(), b));
    return a;
}

/** Return the product of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator*(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator* of undefined");
    Internal::match_types(a, b);
    return new Internal::Mul(a, b);
}
    
/** Modify the first expression to be the product of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator*=(Expr &a, Expr b) {
    assert(a.defined() && b.defined() && "operator*= of undefined");
    a = new Internal::Mul(a, cast(a.type(), b));
    return a;
}

/** Return the ratio of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator/(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator/ of undefined");
    Internal::match_types(a, b);
    return new Internal::Div(a, b);
}

/** Modify the first expression to be the ratio of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator/=(Expr &a, Expr b) {
    assert(a.defined() && b.defined() && "operator/= of undefined");
    a = new Internal::Div(a, cast(a.type(), b));
    return a;
}

/** Return the first argument reduced modulo the second, doing any
 * necessary type coercion using \ref Internal::match_types */
inline Expr operator%(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator% of undefined");
    Internal::match_types(a, b);
    return new Internal::Mod(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is greater than the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator>(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator> of undefined");
    Internal::match_types(a, b);
    return new Internal::GT(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is less than the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator<(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator< of undefined");
    Internal::match_types(a, b);
    return new Internal::LT(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is less than or equal to the second, after doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator<=(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator<= of undefined");
    Internal::match_types(a, b);
    return new Internal::LE(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is greater than or equal to the second, after doing any necessary
 * type coercion using \ref Internal::match_types */

inline Expr operator>=(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator>= of undefined");
    Internal::match_types(a, b);
    return new Internal::GE(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is equal to the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator==(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator== of undefined");
    Internal::match_types(a, b);
    return new Internal::EQ(a, b);
}

/** Return a boolean expression that tests whether the first argument
 * is not equal to the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator!=(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "operator!= of undefined");
    Internal::match_types(a, b);
    return new Internal::NE(a, b);
}

/** Returns the logical and of the two arguments */
inline Expr operator&&(Expr a, Expr b) {
    return new Internal::And(a, b);
}

/** Returns the logical or of the two arguments */
inline Expr operator||(Expr a, Expr b) {
    return new Internal::Or(a, b);
}

/** Returns the logical not the argument */
inline Expr operator!(Expr a) {
    return new Internal::Not(a);
}

/** Returns an expression representing the greater of the two
 * arguments, after doing any necessary type coercion using 
 * \ref Internal::match_types */
inline Expr max(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "max of undefined");
    Internal::match_types(a, b);
    return new Internal::Max(a, b);
}

/** Returns an expression representing the lesser of the two
 * arguments, after doing any necessary type coercion using 
 * \ref Internal::match_types */
inline Expr min(Expr a, Expr b) {
    assert(a.defined() && b.defined() && "min of undefined");
    Internal::match_types(a, b);
    return new Internal::Min(a, b);
}

/** Clamps an expression to lie within the given bounds. The bounds
 * are type-cast to match the expression. */
inline Expr clamp(Expr a, Expr min_val, Expr max_val) {
    assert(a.defined() && min_val.defined() && max_val.defined() &&
           "clamp of undefined");
    min_val = cast(a.type(), min_val);
    max_val = cast(a.type(), max_val);
    return new Internal::Max(new Internal::Min(a, max_val), min_val);
}

/** Returns the absolute value of a signed integer or floating-point
 * expression */
inline Expr abs(Expr a) {
    assert(a.defined() && "abs of undefined");
    if (a.type() == Int(8))
        return new Internal::Call(Int(8), "abs_i8", vec(a));
    if (a.type() == Int(16)) 
        return new Internal::Call(Int(16), "abs_i16", vec(a));
    if (a.type() == Int(32)) 
        return new Internal::Call(Int(32), "abs_i32", vec(a));
    if (a.type() == Int(64)) 
        return new Internal::Call(Int(64), "abs_i64", vec(a));
    if (a.type() == Float(32)) 
        return new Internal::Call(Float(32), "abs_f32", vec(a));
    if (a.type() == Float(64)) 
        return new Internal::Call(Float(64), "abs_f64", vec(a));
    assert(false && "Invalid type for abs");
}

/** Returns an expression equivalent to the ternary operator in C. If
 * the first argument is true, then return the second, else return the
 * third. */
inline Expr select(Expr a, Expr b, Expr c) {
    return new Internal::Select(a, b, c);
}

/** Return the sine of a floating-point expression. If the argument is
 * not floating-point, is it cast to Float(32). */
inline Expr sin(Expr x) {
    assert(x.defined() && "sin of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "sin_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "sin_f32", vec(cast<float>(x)));
    }
}

/** Return the arcsine of a floating-point expression. If the argument is
 * not floating-point, is it cast to Float(32). */
inline Expr asin(Expr x) {
    assert(x.defined() && "asin of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "asin_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "asin_f32", vec(cast<float>(x)));
    }
}

/** Return the cosine of a floating-point expression. If the argument
 * is not floating-point, is it cast to Float(32). */
inline Expr cos(Expr x) {
    assert(x.defined() && "cos of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "cos_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "cos_f32", vec(cast<float>(x)));
    }
}

/** Return the arccosine of a floating-point expression. If the argument
 * is not floating-point, is it cast to Float(32). */
inline Expr acos(Expr x) {
    assert(x.defined() && "acos of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "acos_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "acos_f32", vec(cast<float>(x)));
    }
}

/** Return the tangent of a floating-point expression. If the argument is
 * not floating-point, is it cast to Float(32). */
inline Expr tan(Expr x) {
    assert(x.defined() && "tan of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "tan_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "tan_f32", vec(cast<float>(x)));
    }
}

/** Return the arctangent of a floating-point expression. If the argument is
 * not floating-point, is it cast to Float(32). */
inline Expr atan(Expr x) {
    assert(x.defined() && "atan of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "atan_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "atan_f32", vec(cast<float>(x)));
    }
}

/** Return the hyperbolic sine of a floating-point expression.
 *  If the argument is not floating-point, is it cast to Float(32). */
inline Expr sinh(Expr x) {
    assert(x.defined() && "sinh of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "sinh_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "sinh_f32", vec(cast<float>(x)));
    }
}

/** Return the hyperbolic arcsinhe of a floating-point expression.
 * If the argument is not floating-point, is it cast to Float(32). */
inline Expr asinh(Expr x) {
    assert(x.defined() && "asinh of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "asinh_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "asinh_f32", vec(cast<float>(x)));
    }
}

/** Return the hyperbolic cosine of a floating-point expression.
 * If the argument is not floating-point, is it cast to Float(32). */
inline Expr cosh(Expr x) {
    assert(x.defined() && "cosh of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "cosh_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "cosh_f32", vec(cast<float>(x)));
    }
}

/** Return the hyperbolic arccosine of a floating-point expression.
 * If the argument is not floating-point, is it cast to Float(32). */
inline Expr acosh(Expr x) {
    assert(x.defined() && "acosh of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "acosh_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "acosh_f32", vec(cast<float>(x)));
    }
}

/** Return the hyperbolic tangent of a floating-point expression.
 * If the argument is not floating-point, is it cast to Float(32). */
inline Expr tanh(Expr x) {
    assert(x.defined() && "tanh of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "tanh_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "tanh_f32", vec(cast<float>(x)));
    }
}

/** Return the hyperbolic arctangent of a floating-point expression.
 * If the argument is not floating-point, is it cast to Float(32). */
inline Expr atanh(Expr x) {
    assert(x.defined() && "atanh of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "atanh_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "atanh_f32", vec(cast<float>(x)));
    }
}

/** Return the square root of a floating-point expression. If the
 * argument is not floating-point, is it cast to Float(32). */
inline Expr sqrt(Expr x) {
    assert(x.defined() && "sqrt of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "sqrt_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "sqrt_f32", vec(cast<float>(x)));
    }
}

/** Return the square root of a floating-point expression. If the
 * argument is not floating-point, is it cast to Float(32). */
inline Expr hypot(Expr x, Expr y) {
    assert(x.defined() && y.defined() && "hypot of undefined");
    if (x.type() == Float(64)) {
        y = cast<double>(y);
        return new Internal::Call(Float(64), "hypot_f64", vec(x, y));
    } else {
        x = cast<float>(x);
        y = cast<float>(y);
        return new Internal::Call(Float(32), "hypot_f32", vec(x, y));
    }
}

/** Return the exponential of a floating-point expression. If the
 * argument is not floating-point, is it cast to Float(32). */
inline Expr exp(Expr x) {
    assert(x.defined() && "exp of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "exp_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "exp_f32", vec(cast<float>(x)));
    }
}

/** Return the logarithm of a floating-point expression. If the
 * argument is not floating-point, is it cast to Float(32). */
inline Expr log(Expr x) {
    assert(x.defined() && "log of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "log_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "log_f32", vec(cast<float>(x)));
    }
}

/** Return the greatest whole number less than or equal to a
 * floating-point expression. If the argument is not floating-point,
 * is it cast to Float(32). The return value is still in floating
 * point, despite being a whole number. */
inline Expr floor(Expr x) {
    assert(x.defined() && "floor of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "floor_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "floor_f32", vec(cast<float>(x)));
    }
}

/** Return the least whole number greater than or equal to a
 * floating-point expression. If the argument is not floating-point,
 * is it cast to Float(32). The return value is still in floating
 * point, despite being a whole number. */
inline Expr ceil(Expr x) {
    assert(x.defined() && "ceil of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "ceil_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "ceil_f32", vec(cast<float>(x)));
    }
}

/** Return the whole number closest to a floating-point expression. If
 * the argument is not floating-point, is it cast to Float(32). The
 * return value is still in floating point, despite being a whole
 * number. On ties, we round up. */
inline Expr round(Expr x) {
    assert(x.defined() && "round of undefined");
    if (x.type() == Float(64)) {
        return new Internal::Call(Float(64), "round_f64", vec(x));
    } else {
        return new Internal::Call(Float(32), "round_f32", vec(cast<float>(x)));
    }
}

/** Return one floating point expression raised to the power of
 * another. The type of the result is given by the type of the first
 * argument. If the first argument is not a floating-point type, it is
 * cast to Float(32) */
inline Expr pow(Expr x, Expr y) {
    assert(x.defined() && y.defined() && "pow of undefined");
    if (x.type() == Float(64)) {
        y = cast<double>(y);
        return new Internal::Call(Float(64), "pow_f64", vec(x, y));
    } else {
        x = cast<float>(x);
        y = cast<float>(y);
        return new Internal::Call(Float(32), "pow_f32", vec(x, y));
    }
}

}


#endif

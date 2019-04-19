#ifndef HALIDE_IR_OPERATOR_H
#define HALIDE_IR_OPERATOR_H

/** \file
 *
 * Defines various operator overloads and utility functions that make
 * it more pleasant to work with Halide expressions.
 */

#include <atomic>
#include <cmath>

#include "IR.h"
#include "Tuple.h"
#include "Util.h"

namespace Halide {

namespace Internal {
/** Is the expression either an IntImm, a FloatImm, a StringImm, or a
 * Cast of the same, or a Ramp or Broadcast of the same. Doesn't do
 * any constant folding. */
bool is_const(const Expr &e);

/** Is the expression an IntImm, FloatImm of a particular value, or a
 * Cast, or Broadcast of the same. */
bool is_const(const Expr &e, int64_t v);

/** If an expression is an IntImm or a Broadcast of an IntImm, return
 * a pointer to its value. Otherwise returns nullptr. */
const int64_t *as_const_int(const Expr &e);

/** If an expression is a UIntImm or a Broadcast of a UIntImm, return
 * a pointer to its value. Otherwise returns nullptr. */
const uint64_t *as_const_uint(const Expr &e);

/** If an expression is a FloatImm or a Broadcast of a FloatImm,
 * return a pointer to its value. Otherwise returns nullptr. */
const double *as_const_float(const Expr &e);

/** Is the expression a constant integer power of two. Also returns
 * log base two of the expression if it is. Only returns true for
 * integer types. */
bool is_const_power_of_two_integer(const Expr &e, int *bits);

/** Is the expression a const (as defined by is_const), and also
 * strictly greater than zero (in all lanes, if a vector expression) */
bool is_positive_const(const Expr &e);

/** Is the expression a const (as defined by is_const), and also
 * strictly less than zero (in all lanes, if a vector expression) */
bool is_negative_const(const Expr &e);

/** Is the expression a const (as defined by is_const), and also
 * strictly less than zero (in all lanes, if a vector expression) and
 * is its negative value representable. (This excludes the most
 * negative value of the Expr's type from inclusion. Intended to be
 * used when the value will be negated as part of simplification.)
 */
bool is_negative_negatable_const(const Expr &e);

/** Is the expression an undef */
bool is_undef(const Expr &e);

/** Is the expression a const (as defined by is_const), and also equal
 * to zero (in all lanes, if a vector expression) */
bool is_zero(const Expr &e);

/** Is the expression a const (as defined by is_const), and also equal
 * to one (in all lanes, if a vector expression) */
bool is_one(const Expr &e);

/** Is the expression a const (as defined by is_const), and also equal
 * to two (in all lanes, if a vector expression) */
bool is_two(const Expr &e);

/** Is the statement a no-op (which we represent as either an
 * undefined Stmt, or as an Evaluate node of a constant) */
bool is_no_op(const Stmt &s);

/** Does the expression
 * 1) Take on the same value no matter where it appears in a Stmt, and
 * 2) Evaluating it has no side-effects
 */
bool is_pure(const Expr &e);

/** Construct an immediate of the given type from any numeric C++ type. */
// @{
Expr make_const(Type t, int64_t val);
Expr make_const(Type t, uint64_t val);
Expr make_const(Type t, double val);
inline Expr make_const(Type t, int32_t val)   {return make_const(t, (int64_t)val);}
inline Expr make_const(Type t, uint32_t val)  {return make_const(t, (uint64_t)val);}
inline Expr make_const(Type t, int16_t val)   {return make_const(t, (int64_t)val);}
inline Expr make_const(Type t, uint16_t val)  {return make_const(t, (uint64_t)val);}
inline Expr make_const(Type t, int8_t val)    {return make_const(t, (int64_t)val);}
inline Expr make_const(Type t, uint8_t val)   {return make_const(t, (uint64_t)val);}
inline Expr make_const(Type t, bool val)      {return make_const(t, (uint64_t)val);}
inline Expr make_const(Type t, float val)     {return make_const(t, (double)val);}
inline Expr make_const(Type t, float16_t val) {return make_const(t, (double)val);}
// @}

/** Construct a unique indeterminate_expression Expr */
Expr make_indeterminate_expression(Type type);

/** Construct a unique signed_integer_overflow Expr */
Expr make_signed_integer_overflow(Type type);

/** Check if a constant value can be correctly represented as the given type. */
void check_representable(Type t, int64_t val);

/** Construct a boolean constant from a C++ boolean value.
 * May also be a vector if width is given.
 * It is not possible to coerce a C++ boolean to Expr because
 * if we provide such a path then char objects can ambiguously
 * be converted to Halide Expr or to std::string.  The problem
 * is that C++ does not have a real bool type - it is in fact
 * close enough to char that C++ does not know how to distinguish them.
 * make_bool is the explicit coercion. */
Expr make_bool(bool val, int lanes = 1);

/** Construct the representation of zero in the given type */
Expr make_zero(Type t);

/** Construct the representation of one in the given type */
Expr make_one(Type t);

/** Construct the representation of two in the given type */
Expr make_two(Type t);

/** Construct the constant boolean true. May also be a vector of
 * trues, if a lanes argument is given. */
Expr const_true(int lanes = 1);

/** Construct the constant boolean false. May also be a vector of
 * falses, if a lanes argument is given. */
Expr const_false(int lanes = 1);

/** Attempt to cast an expression to a smaller type while provably not
 * losing information. If it can't be done, return an undefined
 * Expr. */
Expr lossless_cast(Type t, Expr e);

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
void match_types(Expr &a, Expr &b);

/** Halide's vectorizable transcendentals. */
// @{
Expr halide_log(Expr a);
Expr halide_exp(Expr a);
Expr halide_erf(Expr a);
// @}

/** Raise an expression to an integer power by repeatedly multiplying
 * it by itself. */
Expr raise_to_integer_power(Expr a, int64_t b);

/** Split a boolean condition into vector of ANDs. If 'cond' is undefined,
 * return an empty vector. */
void split_into_ands(const Expr &cond, std::vector<Expr> &result);

/** A builder to help create Exprs representing halide_buffer_t
 * structs (e.g. foo.buffer) via calls to halide_buffer_init. Fill out
 * the fields and then call build. The resulting Expr will be a call
 * to halide_buffer_init with the struct members as arguments. If the
 * buffer_memory field is undefined, it uses a call to alloca to make
 * some stack memory for the buffer. If the shape_memory field is
 * undefined, it similarly uses stack memory for the shape. If the
 * shape_memory field is null, it uses the dim field already in the
 * buffer. Other unitialized fields will take on a value of zero in
 * the constructed buffer. */
struct BufferBuilder {
    Expr buffer_memory, shape_memory;
    Expr host, device, device_interface;
    Type type;
    int dimensions = 0;
    std::vector<Expr> mins, extents, strides;
    Expr host_dirty, device_dirty;
    Expr build() const;
};

/** If e is a ramp expression with stride, default 1, return the base,
 * otherwise undefined. */
Expr strided_ramp_base(Expr e, int stride = 1);

/** Implementations of division and mod that are specific to Halide.
 * Use these implementations; do not use native C division or mod to
 * simplify Halide expressions. Halide division and modulo satisify
 * the Euclidean definition of division for integers a and b:
 *
 /code
 (a/b)*b + a%b = a
 0 <= a%b < |b|
 /endcode
 *
 */
// @{
template<typename T>
inline T mod_imp(T a, T b) {
    Type t = type_of<T>();
    if (t.is_int()) {
        T r = a % b;
        r = r + (r < 0 ? (T)std::abs((int64_t)b) : 0);
        return r;
    } else {
        return a % b;
    }
}

template<typename T>
inline T div_imp(T a, T b) {
    Type t = type_of<T>();
    if (t.is_int()) {
        int64_t q = a / b;
        int64_t r = a - q * b;
        int64_t bs = b >> (t.bits() - 1);
        int64_t rs = r >> (t.bits() - 1);
        return (T) (q - (rs & bs) + (rs & ~bs));
    } else {
        return a / b;
    }
}
// @}

// Special cases for float, double.
template<> inline float mod_imp<float>(float a, float b) {
    float f = a - b * (floorf(a / b));
    // The remainder has the same sign as b.
    return f;
}
template<> inline double mod_imp<double>(double a, double b) {
    double f = a - b * (std::floor(a / b));
    return f;
}

template<> inline float div_imp<float>(float a, float b) {
    return a/b;
}
template<> inline double div_imp<double>(double a, double b) {
    return a/b;
}

} // namespace Internal

/** Cast an expression to the halide type corresponding to the C++ type T. */
template<typename T>
inline Expr cast(Expr a) {
    return cast(type_of<T>(), std::move(a));
}

/** Cast an expression to a new type. */
inline Expr cast(Type t, Expr a) {
    user_assert(a.defined()) << "cast of undefined Expr\n";
    if (a.type() == t) {
        return a;
    }

    if (t.is_handle() && !a.type().is_handle()) {
        user_error << "Can't cast \"" << a << "\" to a handle. "
                   << "The only legal cast from scalar types to a handle is: "
                   << "reinterpret(Handle(), cast<uint64_t>(" << a << "));\n";
    } else if (a.type().is_handle() && !t.is_handle()) {
        user_error << "Can't cast handle \"" << a << "\" to type " << t << ". "
                   << "The only legal cast from handles to scalar types is: "
                   << "reinterpret(UInt(64), " << a << ");\n";
    }

    // Fold constants early
    if (const int64_t *i = as_const_int(a)) {
        return Internal::make_const(t, *i);
    }
    if (const uint64_t *u = as_const_uint(a)) {
        return Internal::make_const(t, *u);
    }
    if (const double *f = as_const_float(a)) {
        return Internal::make_const(t, *f);
    }

    if (t.is_vector()) {
        if (a.type().is_scalar()) {
            return Internal::Broadcast::make(cast(t.element_of(), std::move(a)), t.lanes());
        } else if (const Internal::Broadcast *b = a.as<Internal::Broadcast>()) {
            internal_assert(b->lanes == t.lanes());
            return Internal::Broadcast::make(cast(t.element_of(), b->value), t.lanes());
        }
    }
    return Internal::Cast::make(t, std::move(a));
}

/** Return the sum of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator+(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator+ of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::Add::make(std::move(a), std::move(b));
}

/** Add an expression and a constant integer. Coerces the type of the
 * integer to match the type of the expression. Errors if the integer
 * cannot be represented in the type of the expression. */
// @{
inline Expr operator+(Expr a, int b) {
    user_assert(a.defined()) << "operator+ of undefined Expr\n";
    Type t = a.type();
    Internal::check_representable(t, b);
    return Internal::Add::make(std::move(a), Internal::make_const(t, b));
}

/** Add a constant integer and an expression. Coerces the type of the
 * integer to match the type of the expression. Errors if the integer
 * cannot be represented in the type of the expression. */
inline Expr operator+(int a, Expr b) {
    user_assert(b.defined()) << "operator+ of undefined Expr\n";
    Type t = b.type();
    Internal::check_representable(t, a);
    return Internal::Add::make(Internal::make_const(t, a), std::move(b));
}

/** Modify the first expression to be the sum of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator+=(Expr &a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator+= of undefined Expr\n";
    Type t = a.type();
    a = Internal::Add::make(std::move(a), cast(t, std::move(b)));
    return a;
}

/** Return the difference of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator-(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator- of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::Sub::make(std::move(a), std::move(b));
}

/** Subtracts a constant integer from an expression. Coerces the type of the
 * integer to match the type of the expression. Errors if the integer
 * cannot be represented in the type of the expression. */
inline Expr operator-(Expr a, int b) {
    user_assert(a.defined()) << "operator- of undefined Expr\n";
    Type t = a.type();
    Internal::check_representable(t, b);
    return Internal::Sub::make(std::move(a), Internal::make_const(t, b));
}

/** Subtracts an expression from a constant integer. Coerces the type
 * of the integer to match the type of the expression. Errors if the
 * integer cannot be represented in the type of the expression. */
inline Expr operator-(int a, Expr b) {
    user_assert(b.defined()) << "operator- of undefined Expr\n";
    Type t = b.type();
    Internal::check_representable(t, a);
    return Internal::Sub::make(Internal::make_const(t, a), std::move(b));
}

/** Return the negative of the argument. Does no type casting, so more
 * formally: return that number which when added to the original,
 * yields zero of the same type. For unsigned integers the negative is
 * still an unsigned integer. E.g. in UInt(8), the negative of 56 is
 * 200, because 56 + 200 == 0 */
inline Expr operator-(Expr a) {
    user_assert(a.defined()) << "operator- of undefined Expr\n";
    Type t = a.type();
    return Internal::Sub::make(Internal::make_zero(t), std::move(a));
}

/** Modify the first expression to be the difference of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator-=(Expr &a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator-= of undefined Expr\n";
    Type t = a.type();
    a = Internal::Sub::make(std::move(a), cast(t, std::move(b)));
    return a;
}

/** Return the product of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator*(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator* of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::Mul::make(std::move(a), std::move(b));
}

/** Multiply an expression and a constant integer. Coerces the type of the
 * integer to match the type of the expression. Errors if the integer
 * cannot be represented in the type of the expression. */
inline Expr operator*(const Expr &a, int b) {
    user_assert(a.defined()) << "operator* of undefined Expr\n";
    Type t = a.type();
    Internal::check_representable(t, b);
    return Internal::Mul::make(std::move(a), Internal::make_const(t, b));
}

/** Multiply a constant integer and an expression. Coerces the type of
 * the integer to match the type of the expression. Errors if the
 * integer cannot be represented in the type of the expression. */
inline Expr operator*(int a, Expr b) {
    user_assert(b.defined()) << "operator* of undefined Expr\n";
    Type t = b.type();
    Internal::check_representable(t, a);
    return Internal::Mul::make(Internal::make_const(t, a), std::move(b));
}

/** Modify the first expression to be the product of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
inline Expr &operator*=(Expr &a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator*= of undefined Expr\n";
    Type t = a.type();
    a = Internal::Mul::make(std::move(a), cast(t, std::move(b)));
    return a;
}

/** Return the ratio of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types. Note that signed integer
 * division in Halide rounds towards minus infinity, unlike C, which
 * rounds towards zero. */
inline Expr operator/(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator/ of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::Div::make(std::move(a), std::move(b));
}

/** Modify the first expression to be the ratio of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. Note that signed integer division in Halide
 * rounds towards minus infinity, unlike C, which rounds towards
 * zero. */
inline Expr &operator/=(Expr &a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator/= of undefined Expr\n";
    Type t = a.type();
    a = Internal::Div::make(std::move(a), cast(t, std::move(b)));
    return a;
}

/** Divides an expression by a constant integer. Coerces the type
 * of the integer to match the type of the expression. Errors if the
 * integer cannot be represented in the type of the expression. */
inline Expr operator/(Expr a, int b) {
    user_assert(a.defined()) << "operator/ of undefined Expr\n";
    Type t = a.type();
    Internal::check_representable(t, b);
    return Internal::Div::make(std::move(a), Internal::make_const(t, b));
}

/** Divides a constant integer by an expression. Coerces the type
 * of the integer to match the type of the expression. Errors if the
 * integer cannot be represented in the type of the expression. */
inline Expr operator/(int a, Expr b) {
    user_assert(b.defined()) << "operator- of undefined Expr\n";
    Type t = b.type();
    Internal::check_representable(t, a);
    return Internal::Div::make(Internal::make_const(t, a), std::move(b));
}

/** Return the first argument reduced modulo the second, doing any
 * necessary type coercion using \ref Internal::match_types. For
 * signed integers, the sign of the result matches the sign of the
 * second argument (unlike in C, where it matches the sign of the
 * first argument). For example, this means that x%2 is always either
 * zero or one, even if x is negative.*/
inline Expr operator%(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator% of undefined Expr\n";
    user_assert(!Internal::is_zero(b)) << "operator% with constant 0 modulus\n";
    Internal::match_types(a, b);
    return Internal::Mod::make(std::move(a), std::move(b));
}

/** Mods an expression by a constant integer. Coerces the type
 * of the integer to match the type of the expression. Errors if the
 * integer cannot be represented in the type of the expression. */
inline Expr operator%(Expr a, int b) {
    user_assert(a.defined()) << "operator% of undefined Expr\n";
    user_assert(b != 0) << "operator% with constant 0 modulus\n";
    Type t = a.type();
    Internal::check_representable(t, b);
    return Internal::Mod::make(std::move(a), Internal::make_const(t, b));
}
/** Mods a constant integer by an expression. Coerces the type
 * of the integer to match the type of the expression. Errors if the
 * integer cannot be represented in the type of the expression. */
inline Expr operator%(int a, const Expr &b) {
    user_assert(b.defined()) << "operator% of undefined Expr\n";
    user_assert(!Internal::is_zero(b)) << "operator% with constant 0 modulus\n";
    Type t = b.type();
    Internal::check_representable(t, a);
    return Internal::Mod::make(Internal::make_const(t, a), std::move(b));
}

/** Return a boolean expression that tests whether the first argument
 * is greater than the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator>(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator> of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::GT::make(std::move(a), std::move(b));
}

/** Return a boolean expression that tests whether an expression is
 * greater than a constant integer. Coerces the integer to the type of
 * the expression. Errors if the integer is not representable in that
 * type. */
inline Expr operator>(Expr a, int b) {
    user_assert(a.defined()) << "operator> of undefined Expr\n";
    Type t = a.type();
    Internal::check_representable(t, b);
    return Internal::GT::make(std::move(a), Internal::make_const(t, b));
}

/** Return a boolean expression that tests whether a constant integer is
 * greater than an expression. Coerces the integer to the type of
 * the expression. Errors if the integer is not representable in that
 * type. */
inline Expr operator>(int a, Expr b) {
    user_assert(b.defined()) << "operator> of undefined Expr\n";
    Type t = b.type();
    Internal::check_representable(t, a);
    return Internal::GT::make(Internal::make_const(t, a), std::move(b));
}

/** Return a boolean expression that tests whether the first argument
 * is less than the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator<(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator< of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::LT::make(std::move(a), std::move(b));
}

/** Return a boolean expression that tests whether an expression is
 * less than a constant integer. Coerces the integer to the type of
 * the expression. Errors if the integer is not representable in that
 * type. */
inline Expr operator<(Expr a, int b) {
    user_assert(a.defined()) << "operator< of undefined Expr\n";
    Type t = a.type();
    Internal::check_representable(t, b);
    return Internal::LT::make(std::move(a), Internal::make_const(t, b));
}

/** Return a boolean expression that tests whether a constant integer is
 * less than an expression. Coerces the integer to the type of
 * the expression. Errors if the integer is not representable in that
 * type. */
inline Expr operator<(int a, Expr b) {
    user_assert(b.defined()) << "operator< of undefined Expr\n";
    Type t = b.type();
    Internal::check_representable(t, a);
    return Internal::LT::make(Internal::make_const(t, a), std::move(b));
}

/** Return a boolean expression that tests whether the first argument
 * is less than or equal to the second, after doing any necessary type
 * coercion using \ref Internal::match_types */
inline Expr operator<=(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator<= of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::LE::make(std::move(a), std::move(b));
}

/** Return a boolean expression that tests whether an expression is
 * less than or equal to a constant integer. Coerces the integer to
 * the type of the expression. Errors if the integer is not
 * representable in that type. */
inline Expr operator<=(Expr a, int b) {
    user_assert(a.defined()) << "operator<= of undefined Expr\n";
    Type t = a.type();
    Internal::check_representable(t, b);
    return Internal::LE::make(std::move(a), Internal::make_const(t, b));
}

/** Return a boolean expression that tests whether a constant integer
 * is less than or equal to an expression. Coerces the integer to the
 * type of the expression. Errors if the integer is not representable
 * in that type. */
inline Expr operator<=(int a, Expr b) {
    user_assert(b.defined()) << "operator<= of undefined Expr\n";
    Type t = b.type();
    Internal::check_representable(t, a);
    return Internal::LE::make(Internal::make_const(t, a), std::move(b));
}

/** Return a boolean expression that tests whether the first argument
 * is greater than or equal to the second, after doing any necessary
 * type coercion using \ref Internal::match_types */
inline Expr operator>=(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator>= of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::GE::make(std::move(a), std::move(b));
}

/** Return a boolean expression that tests whether an expression is
 * greater than or equal to a constant integer. Coerces the integer to
 * the type of the expression. Errors if the integer is not
 * representable in that type. */
inline Expr operator>=(Expr a, int b) {
    user_assert(a.defined()) << "operator>= of undefined Expr\n";
    Type t = a.type();
    Internal::check_representable(t, b);
    return Internal::GE::make(a, Internal::make_const(t, b));
}

/** Return a boolean expression that tests whether a constant integer
 * is greater than or equal to an expression. Coerces the integer to the
 * type of the expression. Errors if the integer is not representable
 * in that type. */
inline Expr operator>=(int a, Expr b) {
    user_assert(b.defined()) << "operator>= of undefined Expr\n";
    Type t = b.type();
    Internal::check_representable(t, a);
    return Internal::GE::make(Internal::make_const(t, a), b);
}

/** Return a boolean expression that tests whether the first argument
 * is equal to the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator==(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator== of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::EQ::make(std::move(a), std::move(b));
}

/** Return a boolean expression that tests whether an expression is
 * equal to a constant integer. Coerces the integer to the type of the
 * expression. Errors if the integer is not representable in that
 * type. */
inline Expr operator==(Expr a, int b) {
    user_assert(a.defined()) << "operator== of undefined Expr\n";
    Type t = a.type();
    Internal::check_representable(t, b);
    return Internal::EQ::make(std::move(a), Internal::make_const(t, b));
}

/** Return a boolean expression that tests whether a constant integer
 * is equal to an expression. Coerces the integer to the type of the
 * expression. Errors if the integer is not representable in that
 * type. */
inline Expr operator==(int a, Expr b) {
    user_assert(b.defined()) << "operator== of undefined Expr\n";
    Type t = b.type();
    Internal::check_representable(t, a);
    return Internal::EQ::make(Internal::make_const(t, a), std::move(b));
}

/** Return a boolean expression that tests whether the first argument
 * is not equal to the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
inline Expr operator!=(Expr a, Expr b) {
    user_assert(a.defined() && b.defined()) << "operator!= of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::NE::make(std::move(a), std::move(b));
}

/** Return a boolean expression that tests whether an expression is
 * not equal to a constant integer. Coerces the integer to the type of
 * the expression. Errors if the integer is not representable in that
 * type. */
inline Expr operator!=(Expr a, int b) {
    user_assert(a.defined()) << "operator!= of undefined Expr\n";
    Type t = a.type();
    Internal::check_representable(t, b);
    return Internal::NE::make(std::move(a), Internal::make_const(t, b));
}

/** Return a boolean expression that tests whether a constant integer
 * is not equal to an expression. Coerces the integer to the type of
 * the expression. Errors if the integer is not representable in that
 * type. */
inline Expr operator!=(int a, Expr b) {
    user_assert(b.defined()) << "operator!= of undefined Expr\n";
    Type t = b.type();
    Internal::check_representable(t, a);
    return Internal::NE::make(Internal::make_const(t, a), std::move(b));
}

/** Returns the logical and of the two arguments */
inline Expr operator&&(Expr a, Expr b) {
    Internal::match_types(a, b);
    return Internal::And::make(std::move(a), std::move(b));
}

/** Logical and of an Expr and a bool. Either returns the Expr or an
 * Expr representing false, depending on the bool. */
// @{
inline Expr operator&&(const Expr &a, bool b) {
    internal_assert(a.defined()) << "operator&& of undefined Expr\n";
    internal_assert(a.type().is_bool()) << "operator&& of Expr of type " << a.type() << "\n";
    if (b) {
        return a;
    } else {
        return Internal::make_zero(a.type());
    }
}
inline Expr operator&&(bool a, const Expr &b) {
    return std::move(b) && a;
}
// @}

/** Returns the logical or of the two arguments */
inline Expr operator||(Expr a, Expr b) {
    Internal::match_types(a, b);
    return Internal::Or::make(std::move(a), std::move(b));
}

/** Logical or of an Expr and a bool. Either returns the Expr or an
 * Expr representing true, depending on the bool. */
// @{
inline Expr operator||(const Expr &a, bool b) {
    internal_assert(a.defined()) << "operator|| of undefined Expr\n";
    internal_assert(a.type().is_bool()) << "operator|| of Expr of type " << a.type() << "\n";
    if (b) {
        return Internal::make_one(a.type());
    } else {
        return a;
    }
}
inline Expr operator||(bool a, const Expr &b) {
    return b || a;
}
// @}


/** Returns the logical not the argument */
inline Expr operator!(Expr a) {
    return Internal::Not::make(std::move(a));
}

/** Returns an expression representing the greater of the two
 * arguments, after doing any necessary type coercion using
 * \ref Internal::match_types. Vectorizes cleanly on most platforms
 * (with the exception of integer types on x86 without SSE4). */
inline Expr max(Expr a, Expr b) {
    user_assert(a.defined() && b.defined())
        << "max of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::Max::make(std::move(a), std::move(b));
}

/** Returns an expression representing the greater of an expression
 * and a constant integer.  The integer is coerced to the type of the
 * expression. Errors if the integer is not representable as that
 * type. Vectorizes cleanly on most platforms (with the exception of
 * integer types on x86 without SSE4). */
inline Expr max(Expr a, int b) {
    user_assert(a.defined()) << "max of undefined Expr\n";
    Type t = a.type();
    Internal::check_representable(t, b);
    return Internal::Max::make(std::move(a), Internal::make_const(t, b));
}


/** Returns an expression representing the greater of a constant
 * integer and an expression. The integer is coerced to the type of
 * the expression. Errors if the integer is not representable as that
 * type. Vectorizes cleanly on most platforms (with the exception of
 * integer types on x86 without SSE4). */
inline Expr max(int a, Expr b) {
    user_assert(b.defined()) << "max of undefined Expr\n";
    Type t = b.type();
    Internal::check_representable(t, a);
    return Internal::Max::make(Internal::make_const(t, a), std::move(b));
}

inline Expr max(float a, Expr b) {return max(Expr(a), std::move(b));}
inline Expr max(Expr a, float b) {return max(std::move(a), Expr(b));}

/** Returns an expression representing the greater of an expressions
 * vector, after doing any necessary type coersion using
 * \ref Internal::match_types. Vectorizes cleanly on most platforms
 * (with the exception of integer types on x86 without SSE4).
 * The expressions are folded from right ie. max(.., max(.., ..)).
 * The arguments can be any mix of types but must all be convertible to Expr. */
template<typename A, typename B, typename C, typename... Rest,
         typename std::enable_if<Halide::Internal::all_are_convertible<Expr, Rest...>::value>::type* = nullptr>
inline Expr max(A &&a, B &&b, C &&c, Rest&&... rest) {
    return max(std::forward<A>(a), max(std::forward<B>(b), std::forward<C>(c), std::forward<Rest>(rest)...));
}

inline Expr min(Expr a, Expr b) {
    user_assert(a.defined() && b.defined())
        << "min of undefined Expr\n";
    Internal::match_types(a, b);
    return Internal::Min::make(std::move(a), std::move(b));
}

/** Returns an expression representing the lesser of an expression
 * and a constant integer.  The integer is coerced to the type of the
 * expression. Errors if the integer is not representable as that
 * type. Vectorizes cleanly on most platforms (with the exception of
 * integer types on x86 without SSE4). */
inline Expr min(Expr a, int b) {
    user_assert(a.defined()) << "max of undefined Expr\n";
    Type t = a.type();
    Internal::check_representable(t, b);
    return Internal::Min::make(std::move(a), Internal::make_const(t, b));
}

/** Returns an expression representing the lesser of a constant
 * integer and an expression. The integer is coerced to the type of
 * the expression. Errors if the integer is not representable as that
 * type. Vectorizes cleanly on most platforms (with the exception of
 * integer types on x86 without SSE4). */
inline Expr min(int a, Expr b) {
    user_assert(b.defined()) << "max of undefined Expr\n";
    Type t = b.type();
    Internal::check_representable(t, a);
    return Internal::Min::make(Internal::make_const(t, a), std::move(b));
}

inline Expr min(float a, Expr b) {return min(Expr(a), std::move(b));}
inline Expr min(Expr a, float b) {return min(std::move(a), Expr(b));}

/** Returns an expression representing the lesser of an expressions
 * vector, after doing any necessary type coersion using
 * \ref Internal::match_types. Vectorizes cleanly on most platforms
 * (with the exception of integer types on x86 without SSE4).
 * The expressions are folded from right ie. min(.., min(.., ..)).
 * The arguments can be any mix of types but must all be convertible to Expr. */
template<typename A, typename B, typename C, typename... Rest,
         typename std::enable_if<Halide::Internal::all_are_convertible<Expr, Rest...>::value>::type* = nullptr>
inline Expr min(A &&a, B &&b, C &&c, Rest&&... rest) {
    return min(std::forward<A>(a), min(std::forward<B>(b), std::forward<C>(c), std::forward<Rest>(rest)...));
}

/** Operators on floats treats those floats as Exprs. Making these
 * explicit prevents implicit float->int casts that might otherwise
 * occur. */
// @{
inline Expr operator+(Expr a, float b) {return std::move(a) + Expr(b);}
inline Expr operator+(float a, Expr b) {return Expr(a) + std::move(b);}
inline Expr operator-(Expr a, float b) {return std::move(a) - Expr(b);}
inline Expr operator-(float a, Expr b) {return Expr(a) - std::move(b);}
inline Expr operator*(Expr a, float b) {return std::move(a) * Expr(b);}
inline Expr operator*(float a, Expr b) {return Expr(a) * std::move(b);}
inline Expr operator/(Expr a, float b) {return std::move(a) / Expr(b);}
inline Expr operator/(float a, Expr b) {return Expr(a) / std::move(b);}
inline Expr operator%(Expr a, float b) {return std::move(a) % Expr(b);}
inline Expr operator%(float a, Expr b) {return Expr(a) % std::move(b);}
inline Expr operator>(Expr a, float b) {return std::move(a) > Expr(b);}
inline Expr operator>(float a, Expr b) {return Expr(a) > std::move(b);}
inline Expr operator<(Expr a, float b) {return std::move(a) < Expr(b);}
inline Expr operator<(float a, Expr b) {return Expr(a) < std::move(b);}
inline Expr operator>=(Expr a, float b) {return std::move(a) >= Expr(b);}
inline Expr operator>=(float a, Expr b) {return Expr(a) >= std::move(b);}
inline Expr operator<=(Expr a, float b) {return std::move(a) <= Expr(b);}
inline Expr operator<=(float a, Expr b) {return Expr(a) <= std::move(b);}
inline Expr operator==(Expr a, float b) {return std::move(a) == Expr(b);}
inline Expr operator==(float a, Expr b) {return Expr(a) == std::move(b);}
inline Expr operator!=(Expr a, float b) {return std::move(a) != Expr(b);}
inline Expr operator!=(float a, Expr b) {return Expr(a) != std::move(b);}
// @}

/** Clamps an expression to lie within the given bounds. The bounds
 * are type-cast to match the expression. Vectorizes as well as min/max. */
inline Expr clamp(Expr a, Expr min_val, Expr max_val) {
    user_assert(a.defined() && min_val.defined() && max_val.defined())
        << "clamp of undefined Expr\n";
    Expr n_min_val = lossless_cast(a.type(), min_val);
    user_assert(n_min_val.defined())
        << "Type mismatch in call to clamp. First argument ("
        << a << ") has type " << a.type() << ", but second argument ("
        << min_val << ") has type " << min_val.type() << ". Use an explicit cast.\n";
    Expr n_max_val = lossless_cast(a.type(), max_val);
    user_assert(n_max_val.defined())
        << "Type mismatch in call to clamp. First argument ("
        << a << ") has type " << a.type() << ", but third argument ("
        << max_val << ") has type " << max_val.type() << ". Use an explicit cast.\n";
    return Internal::Max::make(Internal::Min::make(std::move(a), std::move(n_max_val)), std::move(n_min_val));
}

/** Returns the absolute value of a signed integer or floating-point
 * expression. Vectorizes cleanly. Unlike in C, abs of a signed
 * integer returns an unsigned integer of the same bit width. This
 * means that abs of the most negative integer doesn't overflow. */
inline Expr abs(Expr a) {
    user_assert(a.defined())
        << "abs of undefined Expr\n";
    Type t = a.type();
    if (t.is_uint()) {
        user_warning << "Warning: abs of an unsigned type is a no-op\n";
        return a;
    }
    return Internal::Call::make(t.with_code(t.is_int() ? Type::UInt : t.code()),
                                Internal::Call::abs, {std::move(a)}, Internal::Call::PureIntrinsic);
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
        return abs(std::move(a) - std::move(b));
    }

    // The argument may be signed, but the return type is unsigned.
    return Internal::Call::make(t.with_code(t.is_int() ? Type::UInt : t.code()),
                                Internal::Call::absd, {std::move(a), std::move(b)},
                                Internal::Call::PureIntrinsic);
}

/** Returns an expression similar to the ternary operator in C, except
 * that it always evaluates all arguments. If the first argument is
 * true, then return the second, else return the third. Typically
 * vectorizes cleanly, but benefits from SSE41 or newer on x86. */
Expr select(Expr condition, Expr true_value, Expr false_value);

/** A multi-way variant of select similar to a switch statement in C,
 * which can accept multiple conditions and values in pairs. Evaluates
 * to the first value for which the condition is true. Returns the
 * final value if all conditions are false. */
template<typename... Args,
         typename std::enable_if<Halide::Internal::all_are_convertible<Expr, Args...>::value>::type* = nullptr>
inline Expr select(Expr c0, Expr v0, Expr c1, Expr v1, Args&&... args) {
    return select(std::move(c0), std::move(v0), select(std::move(c1), std::move(v1), std::forward<Args>(args)...));
}

/** Equivalent of ternary select(), but taking/returning tuples. If the condition is
 * a Tuple, it must match the size of the true and false Tuples. */
// @{
Tuple tuple_select(const Tuple &condition, const Tuple &true_value, const Tuple &false_value);
Tuple tuple_select(const Expr &condition, const Tuple &true_value, const Tuple &false_value);
// @}

/** Equivalent of multiway select(), but taking/returning tuples. If the condition is
 * a Tuple, it must match the size of the true and false Tuples. */
// @{
template<typename... Args>
inline Tuple tuple_select(const Tuple &c0, const Tuple &v0, const Tuple &c1, const Tuple &v1, Args&&... args) {
    return tuple_select(c0, v0, tuple_select(c1, v1, std::forward<Args>(args)...));
}

template<typename... Args>
inline Tuple tuple_select(const Expr &c0, const Tuple &v0, const Expr &c1, const Tuple &v1, Args&&... args) {
    return tuple_select(c0, v0, tuple_select(c1, v1, std::forward<Args>(args)...));
}
// @}


// TODO: Implement support for *_f16 external functions in various backends.
// No backend supports these yet.

/** Return the sine of a floating-point expression. If the argument is
 * not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
inline Expr sin(Expr x) {
    user_assert(x.defined()) << "sin of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "sin_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "sin_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "sin_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
    }
}

/** Return the arcsine of a floating-point expression. If the argument
 * is not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
inline Expr asin(Expr x) {
    user_assert(x.defined()) << "asin of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "asin_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "asin_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "asin_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
    }
}

/** Return the cosine of a floating-point expression. If the argument
 * is not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
inline Expr cos(Expr x) {
    user_assert(x.defined()) << "cos of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "cos_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "cos_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "cos_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
    }
}

/** Return the arccosine of a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). Does not
 * vectorize well. */
inline Expr acos(Expr x) {
    user_assert(x.defined()) << "acos of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "acos_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "acos_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "acos_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
    }
}

/** Return the tangent of a floating-point expression. If the argument
 * is not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
inline Expr tan(Expr x) {
    user_assert(x.defined()) << "tan of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "tan_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "tan_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "tan_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
    }
}

/** Return the arctangent of a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). Does not
 * vectorize well. */
inline Expr atan(Expr x) {
    user_assert(x.defined()) << "atan of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "atan_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "atan_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "atan_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
    }
}

/** Return the angle of a floating-point gradient. If the argument is
 * not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
inline Expr atan2(Expr y, Expr x) {
    user_assert(x.defined() && y.defined()) << "atan2 of undefined Expr\n";

    if (y.type() == Float(64)) {
        x = cast<double>(x);
        return Internal::Call::make(Float(64), "atan2_f64", {std::move(y), std::move(x)}, Internal::Call::PureExtern);
    } else if (y.type() == Float(16)) {
        x = cast<float16_t>(x);
        return Internal::Call::make(Float(16), "atan2_f16", {std::move(y), std::move(x)}, Internal::Call::PureExtern);
    } else {
        y = cast<float>(y);
        x = cast<float>(x);
        return Internal::Call::make(Float(32), "atan2_f32", {std::move(y), std::move(x)}, Internal::Call::PureExtern);
    }
}

/** Return the hyperbolic sine of a floating-point expression.  If the
 *  argument is not floating-point, it is cast to Float(32). Does not
 *  vectorize well. */
inline Expr sinh(Expr x) {
    user_assert(x.defined()) << "sinh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "sinh_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "sinh_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "sinh_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
    }
}

/** Return the hyperbolic arcsinhe of a floating-point expression.  If
 * the argument is not floating-point, it is cast to Float(32). Does
 * not vectorize well. */
inline Expr asinh(Expr x) {
    user_assert(x.defined()) << "asinh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "asinh_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "asinh_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "asinh_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
    }
}

/** Return the hyperbolic cosine of a floating-point expression.  If
 * the argument is not floating-point, it is cast to Float(32). Does
 * not vectorize well. */
inline Expr cosh(Expr x) {
    user_assert(x.defined()) << "cosh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "cosh_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "cosh_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "cosh_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
    }
}

/** Return the hyperbolic arccosine of a floating-point expression.
 * If the argument is not floating-point, it is cast to
 * Float(32). Does not vectorize well. */
inline Expr acosh(Expr x) {
    user_assert(x.defined()) << "acosh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "acosh_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "acosh_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "acosh_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
    }
}

/** Return the hyperbolic tangent of a floating-point expression.  If
 * the argument is not floating-point, it is cast to Float(32). Does
 * not vectorize well. */
inline Expr tanh(Expr x) {
    user_assert(x.defined()) << "tanh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "tanh_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "tanh_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "tanh_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
    }
}

/** Return the hyperbolic arctangent of a floating-point expression.
 * If the argument is not floating-point, it is cast to
 * Float(32). Does not vectorize well. */
inline Expr atanh(Expr x) {
    user_assert(x.defined()) << "atanh of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "atanh_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "atanh_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "atanh_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
    }
}

/** Return the square root of a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). Typically
 * vectorizes cleanly. */
inline Expr sqrt(Expr x) {
    user_assert(x.defined()) << "sqrt of undefined Expr\n";
    if (x.type() == Float(64)) {
        return Internal::Call::make(Float(64), "sqrt_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "sqrt_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "sqrt_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
    }
}

/** Return the square root of the sum of the squares of two
 * floating-point expressions. If the argument is not floating-point,
 * it is cast to Float(32). Vectorizes cleanly. */
inline Expr hypot(Expr x, Expr y) {
    return sqrt(x * x + y * y);
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
        return Internal::Call::make(Float(64), "exp_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "exp_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "exp_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
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
        return Internal::Call::make(Float(64), "log_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        return Internal::Call::make(Float(16), "log_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        return Internal::Call::make(Float(32), "log_f32", {cast<float>(std::move(x))}, Internal::Call::PureExtern);
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

    if (const int64_t *i = as_const_int(y)) {
        return raise_to_integer_power(std::move(x), *i);
    }

    if (x.type() == Float(64)) {
        y = cast<double>(std::move(y));
        return Internal::Call::make(Float(64), "pow_f64", {std::move(x), std::move(y)}, Internal::Call::PureExtern);
    } else if (x.type() == Float(16)) {
        y = cast<float16_t>(std::move(y));
        return Internal::Call::make(Float(16), "pow_f16", {std::move(x), std::move(y)}, Internal::Call::PureExtern);
    } else {
        x = cast<float>(std::move(x));
        y = cast<float>(std::move(y));
        return Internal::Call::make(Float(32), "pow_f32", {std::move(x), std::move(y)}, Internal::Call::PureExtern);
    }
}

/** Evaluate the error function erf. Only available for
 * Float(32). Accurate up to the last three bits of the
 * mantissa. Vectorizes cleanly. */
inline Expr erf(Expr x) {
    user_assert(x.defined()) << "erf of undefined Expr\n";
    user_assert(x.type() == Float(32)) << "erf only takes float arguments\n";
    return Internal::halide_erf(std::move(x));
}

// Fast approximation to some trigonometric functions for Float(32)
Expr fast_sin(Expr x);
Expr fast_cos(Expr x);

/** Fast approximate cleanly vectorizable log for Float(32). Returns
 * nonsense for x <= 0.0f. Accurate up to the last 5 bits of the
 * mantissa. Vectorizes cleanly. */
Expr fast_log(Expr x);

/** Fast approximate cleanly vectorizable exp for Float(32). Returns
 * nonsense for inputs that would overflow or underflow. Typically
 * accurate up to the last 5 bits of the mantissa. Gets worse when
 * approaching overflow. Vectorizes cleanly. */
Expr fast_exp(Expr x);

/** Fast approximate cleanly vectorizable pow for Float(32). Returns
 * nonsense for x < 0.0f. Accurate up to the last 5 bits of the
 * mantissa for typical exponents. Gets worse when approaching
 * overflow. Vectorizes cleanly. */
inline Expr fast_pow(Expr x, Expr y) {
    if (const int64_t *i = as_const_int(y)) {
        return raise_to_integer_power(std::move(x), *i);
    }

    x = cast<float>(std::move(x));
    y = cast<float>(std::move(y));
    return select(x == 0.0f, 0.0f, fast_exp(fast_log(x) * std::move(y)));
}

/** Fast approximate inverse for Float(32). Corresponds to the rcpps
 * instruction on x86, and the vrecpe instruction on ARM. Vectorizes
 * cleanly. Note that this can produce slightly different results
 * across different implementations of the same architecture (e.g. AMD vs Intel),
 * even when strict_float is enabled. */
inline Expr fast_inverse(Expr x) {
    user_assert(x.type() == Float(32)) << "fast_inverse only takes float arguments\n";
    Type t = x.type();
    return Internal::Call::make(t, "fast_inverse_f32", {std::move(x)}, Internal::Call::PureExtern);
}

/** Fast approximate inverse square root for Float(32). Corresponds to
 * the rsqrtps instruction on x86, and the vrsqrte instruction on
 * ARM. Vectorizes cleanly. Note that this can produce slightly different results
 * across different implementations of the same architecture (e.g. AMD vs Intel),
 * even when strict_float is enabled. */
inline Expr fast_inverse_sqrt(Expr x) {
    user_assert(x.type() == Float(32)) << "fast_inverse_sqrt only takes float arguments\n";
    Type t = x.type();
    return Internal::Call::make(t, "fast_inverse_sqrt_f32", {std::move(x)}, Internal::Call::PureExtern);
}

/** Return the greatest whole number less than or equal to a
 * floating-point expression. If the argument is not floating-point,
 * it is cast to Float(32). The return value is still in floating
 * point, despite being a whole number. Vectorizes cleanly. */
inline Expr floor(Expr x) {
    user_assert(x.defined()) << "floor of undefined Expr\n";
    Type t = x.type();
    if (t.element_of() == Float(64)) {
        return Internal::Call::make(t, "floor_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (t.element_of() == Float(16)) {
        return Internal::Call::make(t, "floor_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        t = Float(32, t.lanes());
        if (t.is_int() || t.is_uint()) {
            // Already an integer
            return cast(t, std::move(x));
        } else {
            return Internal::Call::make(t, "floor_f32", {cast(t, std::move(x))}, Internal::Call::PureExtern);
        }
    }
}

/** Return the least whole number greater than or equal to a
 * floating-point expression. If the argument is not floating-point,
 * it is cast to Float(32). The return value is still in floating
 * point, despite being a whole number. Vectorizes cleanly. */
inline Expr ceil(Expr x) {
    user_assert(x.defined()) << "ceil of undefined Expr\n";
    Type t = x.type();
    if (t.element_of() == Float(64)) {
        return Internal::Call::make(t, "ceil_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type().element_of() == Float(16)) {
        return Internal::Call::make(t, "ceil_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        t = Float(32, t.lanes());
        if (t.is_int() || t.is_uint()) {
            // Already an integer
            return cast(t, std::move(x));
        } else {
            return Internal::Call::make(t, "ceil_f32", {cast(t, std::move(x))}, Internal::Call::PureExtern);
        }
    }
}

/** Return the whole number closest to a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). The return value
 * is still in floating point, despite being a whole number. On ties, we
 * follow IEEE754 conventions and round to the nearest even number. Vectorizes
 * cleanly. */
inline Expr round(Expr x) {
    user_assert(x.defined()) << "round of undefined Expr\n";
    Type t = x.type();
    if (t.element_of() == Float(64)) {
        return Internal::Call::make(t, "round_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (t.element_of() == Float(16)) {
        return Internal::Call::make(t, "round_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        t = Float(32, t.lanes());
        if (t.is_int() || t.is_uint()) {
            // Already an integer
            return cast(t, std::move(x));
        } else {
            return Internal::Call::make(t, "round_f32", {cast(t, std::move(x))}, Internal::Call::PureExtern);
        }
    }
}

/** Return the integer part of a floating-point expression. If the argument is
 * not floating-point, it is cast to Float(32). The return value is still in
 * floating point, despite being a whole number. Vectorizes cleanly. */
inline Expr trunc(Expr x) {
    user_assert(x.defined()) << "trunc of undefined Expr\n";
    Type t = x.type();
    if (t.element_of() == Float(64)) {
        return Internal::Call::make(t, "trunc_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (t.element_of() == Float(16)) {
        return Internal::Call::make(t, "trunc_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        t = Float(32, t.lanes());
        if (t.is_int() || t.is_uint()) {
            // Already an integer
            return cast(t, std::move(x));
        } else {
            return Internal::Call::make(t, "trunc_f32", {cast(t, std::move(x))}, Internal::Call::PureExtern);
        }
    }
}

/** Returns true if the argument is a Not a Number (NaN). Requires a
  * floating point argument.  Vectorizes cleanly. */
inline Expr is_nan(Expr x) {
    user_assert(x.defined()) << "is_nan of undefined Expr\n";
    user_assert(x.type().is_float()) << "is_nan only works for float";
    Type t = Bool(x.type().lanes());
    if (x.type().element_of() == Float(64)) {
        return Internal::Call::make(t, "is_nan_f64", {std::move(x)}, Internal::Call::PureExtern);
    } else if (x.type().element_of() == Float(16)) {
        return Internal::Call::make(t, "is_nan_f16", {std::move(x)}, Internal::Call::PureExtern);
    } else {
        Type ft = Float(32, t.lanes());
        return Internal::Call::make(t, "is_nan_f32", {cast(ft, std::move(x))}, Internal::Call::PureExtern);
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
    int from_bits = e.type().bits() * e.type().lanes();
    int to_bits = t.bits() * t.lanes();
    user_assert(from_bits == to_bits)
        << "Reinterpret cast from type " << e.type()
        << " which has " << from_bits
        << " bits, to type " << t
        << " which has " << to_bits << " bits\n";
    return Internal::Call::make(t, Internal::Call::reinterpret, {std::move(e)}, Internal::Call::PureIntrinsic);
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
    user_assert(x.type().is_int() || x.type().is_uint())
        << "The first argument to bitwise and must be an integer or unsigned integer";
    user_assert(y.type().is_int() || y.type().is_uint())
        << "The second argument to bitwise and must be an integer or unsigned integer";
    // First widen or narrow, then bitcast.
    if (y.type().bits() != x.type().bits()) {
        y = cast(y.type().with_bits(x.type().bits()), y);
    }
    if (y.type() != x.type()) {
        y = reinterpret(x.type(), y);
    }
    Type t = x.type();
    return Internal::Call::make(t, Internal::Call::bitwise_and, {std::move(x), std::move(y)}, Internal::Call::PureIntrinsic);
}

/** Return the bitwise or of two expressions (which need not have the
 * same type). The type of the result is the type of the first
 * argument. */
inline Expr operator|(Expr x, Expr y) {
    user_assert(x.defined() && y.defined()) << "bitwise or of undefined Expr\n";
    user_assert(x.type().is_int() || x.type().is_uint())
        << "The first argument to bitwise or must be an integer or unsigned integer";
    user_assert(y.type().is_int() || y.type().is_uint())
        << "The second argument to bitwise or must be an integer or unsigned integer";
    // First widen or narrow, then bitcast.
    if (y.type().bits() != x.type().bits()) {
        y = cast(y.type().with_bits(x.type().bits()), y);
    }
    if (y.type() != x.type()) {
        y = reinterpret(x.type(), y);
    }
    Type t = x.type();
    return Internal::Call::make(t, Internal::Call::bitwise_or, {std::move(x), std::move(y)}, Internal::Call::PureIntrinsic);
}

/** Return the bitwise exclusive or of two expressions (which need not
 * have the same type). The type of the result is the type of the
 * first argument. */
inline Expr operator^(Expr x, Expr y) {
    user_assert(x.defined() && y.defined()) << "bitwise xor of undefined Expr\n";
    user_assert(x.type().is_int() || x.type().is_uint())
        << "The first argument to bitwise xor must be an integer or unsigned integer";
    user_assert(y.type().is_int() || y.type().is_uint())
        << "The second argument to bitwise xor must be an integer or unsigned integer";
    // First widen or narrow, then bitcast.
    if (y.type().bits() != x.type().bits()) {
        y = cast(y.type().with_bits(x.type().bits()), y);
    }
    if (y.type() != x.type()) {
        y = reinterpret(x.type(), y);
    }
    Type t = x.type();
    return Internal::Call::make(t, Internal::Call::bitwise_xor, {std::move(x), std::move(y)}, Internal::Call::PureIntrinsic);
}

/** Return the bitwise not of an expression. */
inline Expr operator~(Expr x) {
    user_assert(x.defined()) << "bitwise not of undefined Expr\n";
    user_assert(x.type().is_int() || x.type().is_uint())
        << "Argument to bitwise not must be an integer or unsigned integer";
    Type t = x.type();
    return Internal::Call::make(t, Internal::Call::bitwise_not, {std::move(x)}, Internal::Call::PureIntrinsic);
}

/** Shift the bits of an integer value left. This is actually less
 * efficient than multiplying by 2^n, because Halide's optimization
 * passes understand multiplication, and will compile it to
 * shifting. This operator is only for if you really really need bit
 * shifting (e.g. because the exponent is a run-time parameter). The
 * type of the result is equal to the type of the first argument. Both
 * arguments must have integer type. */
// @{
inline Expr operator<<(Expr x, Expr y) {
    user_assert(x.defined() && y.defined()) << "shift left of undefined Expr\n";
    user_assert(!x.type().is_float()) << "First argument to shift left is a float: " << x << "\n";
    user_assert(!y.type().is_float()) << "Second argument to shift left is a float: " << y << "\n";
    Internal::match_types(x, y);
    Type t = x.type();
    return Internal::Call::make(t, Internal::Call::shift_left, {std::move(x), std::move(y)}, Internal::Call::PureIntrinsic);
}
inline Expr operator<<(Expr x, int y) {
    Type t = x.type();
    Internal::check_representable(t, y);
    return std::move(x) << Internal::make_const(t, y);
}
inline Expr operator<<(int x, Expr y) {
    Type t = y.type();
    Internal::check_representable(t, x);
    return Internal::make_const(t, x) << std::move(y);
}
// @}

/** Shift the bits of an integer value right. Does sign extension for
 * signed integers. This is less efficient than dividing by a power of
 * two. Halide's definition of division (always round to negative
 * infinity) means that all divisions by powers of two get compiled to
 * bit-shifting, and Halide's optimization routines understand
 * division and can work with it. The type of the result is equal to
 * the type of the first argument. Both arguments must have integer
 * type. */
// @{
inline Expr operator>>(Expr x, Expr y) {
    user_assert(x.defined() && y.defined()) << "shift right of undefined Expr\n";
    user_assert(!x.type().is_float()) << "First argument to shift right is a float: " << x << "\n";
    user_assert(!y.type().is_float()) << "Second argument to shift right is a float: " << y << "\n";
    Internal::match_types(x, y);
    Type t = x.type();
    return Internal::Call::make(t, Internal::Call::shift_right, {std::move(x), std::move(y)}, Internal::Call::PureIntrinsic);
}
inline Expr operator>>(Expr x, int y) {
    Type t = x.type();
    Internal::check_representable(t, y);
    return std::move(x) >> Internal::make_const(t, y);
}
inline Expr operator>>(int x, Expr y) {
    Type t = y.type();
    Internal::check_representable(t, x);
    return Internal::make_const(t, x) >> std::move(y);
}
// @}

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
        zero_val = cast(one_val.type(), std::move(zero_val));
    }
    if (as_const_int(one_val)) {
        one_val = cast(zero_val.type(), std::move(one_val));
    }

    user_assert(zero_val.type() == one_val.type())
        << "Can't lerp between " << zero_val << " of type " << zero_val.type()
        << " and " << one_val << " of different type " << one_val.type() << "\n";
    user_assert((weight.type().is_uint() || weight.type().is_float()))
        << "A lerp weight must be an unsigned integer or a float, but "
        << "lerp weight " << weight << " has type " << weight.type() << ".\n";
    user_assert((zero_val.type().is_float() || zero_val.type().lanes() <= 32))
        << "Lerping between 64-bit integers is not supported\n";
    // Compilation error for constant weight that is out of range for integer use
    // as this seems like an easy to catch gotcha.
    if (!zero_val.type().is_float()) {
        const double *const_weight = as_const_float(weight);
        if (const_weight) {
            user_assert(*const_weight >= 0.0 && *const_weight <= 1.0)
                << "Floating-point weight for lerp with integer arguments is "
                << *const_weight << ", which is not in the range [0.0, 1.0].\n";
        }
    }
    Type t = zero_val.type();
    return Internal::Call::make(t, Internal::Call::lerp,
                                {std::move(zero_val), std::move(one_val), std::move(weight)},
                                Internal::Call::PureIntrinsic);
}

/** Count the number of set bits in an expression. */
inline Expr popcount(Expr x) {
    user_assert(x.defined()) << "popcount of undefined Expr\n";
    Type t = x.type();
    user_assert(t.is_uint() || t.is_int())
        << "Argument to popcount must be an integer\n";
    return Internal::Call::make(t, Internal::Call::popcount,
                                {std::move(x)}, Internal::Call::PureIntrinsic);
}

/** Count the number of leading zero bits in an expression. If the expression is
 * zero, the result is the number of bits in the type. */
inline Expr count_leading_zeros(Expr x) {
    user_assert(x.defined()) << "count leading zeros of undefined Expr\n";
    Type t = x.type();
    user_assert(t.is_uint() || t.is_int())
        << "Argument to count_leading_zeros must be an integer\n";
    return Internal::Call::make(t, Internal::Call::count_leading_zeros,
                                {std::move(x)}, Internal::Call::PureIntrinsic);
}

/** Count the number of trailing zero bits in an expression. If the expression is
 * zero, the result is the number of bits in the type. */
inline Expr count_trailing_zeros(Expr x) {
    user_assert(x.defined()) << "count trailing zeros of undefined Expr\n";
    Type t = x.type();
    user_assert(t.is_uint() || t.is_int())
        << "Argument to count_trailing_zeros must be an integer\n";
    return Internal::Call::make(t, Internal::Call::count_trailing_zeros,
                                {std::move(x)}, Internal::Call::PureIntrinsic);
}

/** Divide two integers, rounding towards zero. This is the typical
 * behavior of most hardware architectures, which differs from
 * Halide's division operator, which is Euclidean (rounds towards
 * -infinity). */
inline Expr div_round_to_zero(Expr x, Expr y) {
    user_assert(x.defined()) << "div_round_to_zero of undefined dividend\n";
    user_assert(y.defined()) << "div_round_to_zero of undefined divisor\n";
    Internal::match_types(x, y);
    if (x.type().is_uint()) {
        return std::move(x) / std::move(y);
    }
    user_assert(x.type().is_int()) << "First argument to div_round_to_zero is not an integer: " << x << "\n";
    user_assert(y.type().is_int()) << "Second argument to div_round_to_zero is not an integer: " << y << "\n";
    Type t = x.type();
    return Internal::Call::make(t, Internal::Call::div_round_to_zero,
                                {std::move(x), std::move(y)},
                                Internal::Call::PureIntrinsic);
}

/** Compute the remainder of dividing two integers, when division is
 * rounding toward zero. This is the typical behavior of most hardware
 * architectures, which differs from Halide's mod operator, which is
 * Euclidean (produces the remainder when division rounds towards
 * -infinity). */
inline Expr mod_round_to_zero(Expr x, Expr y) {
    user_assert(x.defined()) << "mod_round_to_zero of undefined dividend\n";
    user_assert(y.defined()) << "mod_round_to_zero of undefined divisor\n";
    Internal::match_types(x, y);
    if (x.type().is_uint()) {
        return std::move(x) % std::move(y);
    }
    user_assert(x.type().is_int()) << "First argument to mod_round_to_zero is not an integer: " << x << "\n";
    user_assert(y.type().is_int()) << "Second argument to mod_round_to_zero is not an integer: " << y << "\n";
    Type t = x.type();
    return Internal::Call::make(t, Internal::Call::mod_round_to_zero,
                                {std::move(x), std::move(y)},
                                Internal::Call::PureIntrinsic);
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
    static std::atomic<int> counter;
    int id = (counter++)*2;

    std::vector<Expr> args;
    if (seed.defined()) {
        user_assert(seed.type() == Int(32))
            << "The seed passed to random_float must have type Int(32), but instead is "
            << seed << " of type " << seed.type() << "\n";
        args.push_back(std::move(seed));
    }
    args.push_back(id);

    // This is (surprisingly) pure - it's a fixed psuedo-random
    // function of its inputs.
    return Internal::Call::make(Float(32), Internal::Call::random,
                                args, Internal::Call::PureIntrinsic);
}

/** Return a random variable representing a uniformly distributed
 * unsigned 32-bit integer. See \ref random_float. Vectorizes cleanly. */
inline Expr random_uint(Expr seed = Expr()) {
    // Random ints get odd IDs
    static std::atomic<int> counter;
    int id = (counter++)*2 + 1;

    std::vector<Expr> args;
    if (seed.defined()) {
        user_assert(seed.type() == Int(32) || seed.type() == UInt(32))
            << "The seed passed to random_int must have type Int(32) or UInt(32), but instead is "
            << seed << " of type " << seed.type() << "\n";
        args.push_back(std::move(seed));
    }
    args.push_back(id);

    return Internal::Call::make(UInt(32), Internal::Call::random,
                                args, Internal::Call::PureIntrinsic);
}

/** Return a random variable representing a uniformly distributed
 * 32-bit integer. See \ref random_float. Vectorizes cleanly. */
inline Expr random_int(Expr seed = Expr()) {
    return cast<int32_t>(random_uint(std::move(seed)));
}

// Secondary args to print can be Exprs or const char *
namespace Internal {
inline HALIDE_NO_USER_CODE_INLINE void collect_print_args(std::vector<Expr> &args) {
}

template<typename ...Args>
inline HALIDE_NO_USER_CODE_INLINE void collect_print_args(std::vector<Expr> &args, const char *arg, Args&&... more_args) {
    args.push_back(Expr(std::string(arg)));
    collect_print_args(args, std::forward<Args>(more_args)...);
}

template<typename ...Args>
inline HALIDE_NO_USER_CODE_INLINE void collect_print_args(std::vector<Expr> &args, Expr arg, Args&&... more_args) {
    args.push_back(std::move(arg));
    collect_print_args(args, std::forward<Args>(more_args)...);
}
}


/** Create an Expr that prints out its value whenever it is
 * evaluated. It also prints out everything else in the arguments
 * list, separated by spaces. This can include string literals. */
//@{
Expr print(const std::vector<Expr> &values);

template <typename... Args>
inline HALIDE_NO_USER_CODE_INLINE Expr print(Expr a, Args&&... args) {
    std::vector<Expr> collected_args = {std::move(a)};
    Internal::collect_print_args(collected_args, std::forward<Args>(args)...);
    return print(collected_args);
}
//@}

/** Create an Expr that prints whenever it is evaluated, provided that
 * the condition is true. */
// @{
Expr print_when(Expr condition, const std::vector<Expr> &values);

template<typename ...Args>
inline HALIDE_NO_USER_CODE_INLINE Expr print_when(Expr condition, Expr a, Args&&... args) {
    std::vector<Expr> collected_args = {std::move(a)};
    Internal::collect_print_args(collected_args, std::forward<Args>(args)...);
    return print_when(std::move(condition), collected_args);
}

// @}

/** Create an Expr that that guarantees a precondition.
 * If 'condition' is true, the return value is equal to the first Expr.
 * If 'condition' is false, halide_error() is called, and the return value
 * is arbitrary. Any additional arguments after the first Expr are stringified
 * and passed as a user-facing message to halide_error(), similar to print().
 *
 * Note that this essentially *always* inserts a runtime check into the
 * generated code (except when the condition can be proven at compile time);
 * as such, it should be avoided inside inner loops, except for debugging
 * or testing purposes. Note also that it does not vectorize cleanly (vector
 * values will be scalarized for the check).
 *
 * However, using this to make assertions about (say) input values
 * can be useful, both in terms of correctness and (potentially) in terms
 * of code generation, e.g.
 \code
 Param<int> p;
 Expr y = require(p > 0, p);
 \endcode
 * will allow the optimizer to assume positive, nonzero values for y.
 */
// @{
Expr require(Expr condition, const std::vector<Expr> &values);

template<typename ...Args>
inline HALIDE_NO_USER_CODE_INLINE Expr require(Expr condition, Expr value, Args&&... args) {
    std::vector<Expr> collected_args = {std::move(value)};
    Internal::collect_print_args(collected_args, std::forward<Args>(args)...);
    return require(std::move(condition), collected_args);
}

namespace Internal {
Expr requirement_failed_error(Expr condition, const std::vector<Expr> &args);
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
                                Internal::Call::PureIntrinsic);
}

template<typename T>
inline Expr undef() {
    return undef(type_of<T>());
}

namespace Internal {
Expr memoize_tag_helper(Expr result, const std::vector<Expr> &cache_key_values);
}  // namespace Internal

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
template<typename ...Args>
inline HALIDE_NO_USER_CODE_INLINE Expr memoize_tag(Expr result, Args&&... args) {
    std::vector<Expr> collected_args{std::forward<Args>(args)...};
    return Internal::memoize_tag_helper(std::move(result), collected_args);
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
 * use the boundary condition helpers in the BoundaryConditions
 * namespace instead.
 */
inline Expr likely(Expr e) {
    Type t = e.type();
    return Internal::Call::make(t, Internal::Call::likely,
                                {std::move(e)}, Internal::Call::PureIntrinsic);
}

/** Equivalent to likely, but only triggers a loop partitioning if
 * found in an innermost loop. */
inline Expr likely_if_innermost(Expr e) {
    Type t = e.type();
    return Internal::Call::make(t, Internal::Call::likely_if_innermost,
                                {std::move(e)}, Internal::Call::PureIntrinsic);
}

/** Cast an expression to the halide type corresponding to the C++
 * type T. As part of the cast, clamp to the minimum and maximum
 * values of the result type. */
template <typename T>
Expr saturating_cast(Expr e) {
    return saturating_cast(type_of<T>(), std::move(e));
}

/** Cast an expression to a new type, clamping to the minimum and
 * maximum values of the result type. */
Expr saturating_cast(Type t, Expr e);

/** Makes a best effort attempt to preserve IEEE floating-point
 * semantics in evaluating an expression. May not be implemented for
 * all backends. (E.g. it is difficult to do this for C++ code
 * generation as it depends on the compiler flags used to compile the
 * generated code. */
inline Expr strict_float(Expr e) {
    Type t = e.type();
    return Internal::Call::make(t, Internal::Call::strict_float,
                                {std::move(e)}, Internal::Call::PureIntrinsic);
}

/** Create an Expr that that promises another Expr is clamped but do
 * not generate code to check the assertion or modify the value. No
 * attempt is made to prove the bound at compile time. (If it is
 * proved false as a result of something else, an error might be
 * generated, but it is also possible the compiler will crash.) The
 * promised bound is used in bounds inference so it will allow
 * satisfying bounds checks as well as possibly aiding optimization.
 *
 * unsafe_promise_clamped returns its first argument, the Expr 'value'
 *
 * This is a very easy way to make Halide generate erroneous code if
 * the bound promises is not kept. Use sparingly when there is no
 * other way to convey the information to the compiler and it is
 * required for a valuable optimization.
 *
 * Unsafe promises can be checked by turning on
 * Target::CheckUnsafePromises. This is intended for debugging only.
 */
Expr unsafe_promise_clamped(Expr value, Expr min, Expr max);

}  // namespace Halide

#endif

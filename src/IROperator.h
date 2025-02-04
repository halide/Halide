#ifndef HALIDE_IR_OPERATOR_H
#define HALIDE_IR_OPERATOR_H

/** \file
 *
 * Defines various operator overloads and utility functions that make
 * it more pleasant to work with Halide expressions.
 */

#include <cmath>
#include <map>
#include <optional>

#include "ConstantInterval.h"
#include "Expr.h"
#include "Scope.h"
#include "Target.h"
#include "Tuple.h"

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
 * a its value. Otherwise returns std::nullopt. */
std::optional<int64_t> as_const_int(const Expr &e);

/** If an expression is a UIntImm or a Broadcast of a UIntImm, return
 * its value. Otherwise returns std::nullopt. */
std::optional<uint64_t> as_const_uint(const Expr &e);

/** If an expression is a FloatImm or a Broadcast of a FloatImm,
 * return its value. Otherwise returns std::nullopt. */
std::optional<double> as_const_float(const Expr &e);

/** Is the expression a constant integer power of two. Returns log base two of
 * the expression if it is, or std::nullopt if not. Also returns std::nullopt
 * for non-integer types. */
// @{
std::optional<int> is_const_power_of_two_integer(const Expr &e);
std::optional<int> is_const_power_of_two_integer(uint64_t);
std::optional<int> is_const_power_of_two_integer(int64_t);
// @}

/** Is the expression a const (as defined by is_const), and also
 * strictly greater than zero (in all lanes, if a vector expression) */
bool is_positive_const(const Expr &e);

/** Is the expression a const (as defined by is_const), and also
 * strictly less than zero (in all lanes, if a vector expression) */
bool is_negative_const(const Expr &e);

/** Is the expression an undef */
bool is_undef(const Expr &e);

/** Is the expression a const (as defined by is_const), and also equal
 * to zero (in all lanes, if a vector expression) */
bool is_const_zero(const Expr &e);

/** Is the expression a const (as defined by is_const), and also equal
 * to one (in all lanes, if a vector expression) */
bool is_const_one(const Expr &e);

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
inline Expr make_const(Type t, int32_t val) {
    return make_const(t, (int64_t)val);
}
inline Expr make_const(Type t, uint32_t val) {
    return make_const(t, (uint64_t)val);
}
inline Expr make_const(Type t, int16_t val) {
    return make_const(t, (int64_t)val);
}
inline Expr make_const(Type t, uint16_t val) {
    return make_const(t, (uint64_t)val);
}
inline Expr make_const(Type t, int8_t val) {
    return make_const(t, (int64_t)val);
}
inline Expr make_const(Type t, uint8_t val) {
    return make_const(t, (uint64_t)val);
}
inline Expr make_const(Type t, bool val) {
    return make_const(t, (uint64_t)val);
}
inline Expr make_const(Type t, float val) {
    return make_const(t, (double)val);
}
inline Expr make_const(Type t, float16_t val) {
    return make_const(t, (double)val);
}
// @}

/** Construct a unique signed_integer_overflow Expr */
Expr make_signed_integer_overflow(Type type);

/** Check if an expression is a signed_integer_overflow */
bool is_signed_integer_overflow(const Expr &expr);

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

/** Attempt to cast an expression to a smaller type while provably not losing
 * information. If it can't be done, return an undefined Expr.
 *
 * Optionally accepts a scope giving the constant bounds of any variables, and a
 * map that gives the constant bounds of exprs already analyzed to avoid redoing
 * work across many calls to lossless_cast. It is not safe to use this optional
 * map in contexts where the same Expr object may take on a different value. For
 * example: (let x = 4 in some_expr_object) + (let x = 5 in
 * the_same_expr_object)).  It is safe to use it after uniquify_variable_names
 * has been run. */
Expr lossless_cast(Type t, Expr e,
                   const Scope<ConstantInterval> &scope = Scope<ConstantInterval>::empty_scope(),
                   std::map<Expr, ConstantInterval, ExprCompare> *cache = nullptr);

/** Attempt to negate x without introducing new IR and without overflow.
 * If it can't be done, return an undefined Expr. */
Expr lossless_negate(const Expr &x);

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

/** Asserts that both expressions are integer types and are either
 * both signed or both unsigned. If one argument is scalar and the
 * other a vector, the scalar is broadcasted to have the same number
 * of lanes as the vector. If one expression is of narrower type than
 * the other, it is widened to the bit width of the wider. */
void match_types_bitwise(Expr &a, Expr &b, const char *op_name);

/** Halide's vectorizable transcendentals. */
// @{
Expr halide_log(const Expr &a);
Expr halide_exp(const Expr &a);
Expr halide_erf(const Expr &a);
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
Expr strided_ramp_base(const Expr &e, int stride = 1);

/** Implementations of division and mod that are specific to Halide.
 * Use these implementations; do not use native C division or mod to
 * simplify Halide expressions. Halide division and modulo satisify
 * the Euclidean definition of division for integers a and b:
 *
 /code
 when b != 0, (a/b)*b + a%b = a
 0 <= a%b < |b|
 /endcode
 *
 * Additionally, mod by zero returns zero, and div by zero returns
 * zero. This makes mod and div total functions.
 */
// @{
template<typename T>
inline T mod_imp(T a, T b) {
    Type t = type_of<T>();
    if (!t.is_float() && b == 0) {
        return 0;
    } else if (t.is_int()) {
        int64_t ia = a;
        int64_t ib = b;
        int64_t a_neg = ia >> 63;
        int64_t b_neg = ib >> 63;
        int64_t b_zero = (ib == 0) ? -1 : 0;
        ia -= a_neg;
        int64_t r = ia % (ib | b_zero);
        r += (a_neg & ((ib ^ b_neg) + ~b_neg));
        r &= ~b_zero;
        return r;
    } else {
        return a % b;
    }
}

template<typename T>
inline T div_imp(T a, T b) {
    Type t = type_of<T>();
    if (!t.is_float() && b == 0) {
        return (T)0;
    } else if (t.is_int()) {
        // Do it as 64-bit
        int64_t ia = a;
        int64_t ib = b;
        int64_t a_neg = ia >> 63;
        int64_t b_neg = ib >> 63;
        int64_t b_zero = (ib == 0) ? -1 : 0;
        ib -= b_zero;
        ia -= a_neg;
        int64_t q = ia / ib;
        q += a_neg & (~b_neg - b_neg);
        q &= ~b_zero;
        return (T)q;
    } else {
        return a / b;
    }
}
// @}

// Special cases for float, double.
template<>
inline float mod_imp<float>(float a, float b) {
    float f = a - b * (floorf(a / b));
    // The remainder has the same sign as b.
    return f;
}
template<>
inline double mod_imp<double>(double a, double b) {
    double f = a - b * (std::floor(a / b));
    return f;
}

template<>
inline float div_imp<float>(float a, float b) {
    return a / b;
}
template<>
inline double div_imp<double>(double a, double b) {
    return a / b;
}

/** Return an Expr that is identical to the input Expr, but with
 * all calls to likely() and likely_if_innermost() removed. */
Expr remove_likelies(const Expr &e);

/** Return a Stmt that is identical to the input Stmt, but with
 * all calls to likely() and likely_if_innermost() removed. */
Stmt remove_likelies(const Stmt &s);

/** Return an Expr that is identical to the input Expr, but with
 * all calls to promise_clamped() and unsafe_promise_clamped() removed. */
Expr remove_promises(const Expr &e);

/** Return a Stmt that is identical to the input Stmt, but with
 * all calls to promise_clamped() and unsafe_promise_clamped() removed. */
Stmt remove_promises(const Stmt &s);

/** If the expression is a tag helper call, remove it and return
 * the tagged expression. If not, returns the expression. */
Expr unwrap_tags(const Expr &e);

template<typename T>
struct is_printable_arg {
    static constexpr bool value = std::is_convertible<T, const char *>::value ||
                                  std::is_convertible<T, Halide::Expr>::value;
};

template<typename... Args>
struct all_are_printable_args : meta_and<is_printable_arg<Args>...> {};

// Secondary args to print can be Exprs or const char *
inline HALIDE_NO_USER_CODE_INLINE void collect_print_args(std::vector<Expr> &args) {
}

template<typename... Args>
inline HALIDE_NO_USER_CODE_INLINE void collect_print_args(std::vector<Expr> &args, const char *arg, Args &&...more_args) {
    args.emplace_back(std::string(arg));
    collect_print_args(args, std::forward<Args>(more_args)...);
}

template<typename... Args>
inline HALIDE_NO_USER_CODE_INLINE void collect_print_args(std::vector<Expr> &args, Expr arg, Args &&...more_args) {
    args.push_back(std::move(arg));
    collect_print_args(args, std::forward<Args>(more_args)...);
}

Expr requirement_failed_error(Expr condition, const std::vector<Expr> &args);

Expr memoize_tag_helper(Expr result, const std::vector<Expr> &cache_key_values);

/** Reset the counters used for random-number seeds in random_float/int/uint.
 * (Note that the counters are incremented for each call, even if a seed is passed in.)
 * This is used for multitarget compilation to ensure that each subtarget gets
 * the same sequence of random numbers. */
void reset_random_counters();

}  // namespace Internal

/** Cast an expression to the halide type corresponding to the C++ type T. */
template<typename T>
inline Expr cast(Expr a) {
    return cast(type_of<T>(), std::move(a));
}

/** Cast an expression to a new type. */
Expr cast(Type t, Expr a);

/** Return the sum of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
Expr operator+(Expr a, Expr b);

/** Add an expression and a constant integer. Coerces the type of the
 * integer to match the type of the expression. Errors if the integer
 * cannot be represented in the type of the expression. */
// @{
Expr operator+(Expr a, int b);

/** Add a constant integer and an expression. Coerces the type of the
 * integer to match the type of the expression. Errors if the integer
 * cannot be represented in the type of the expression. */
Expr operator+(int a, Expr b);

/** Modify the first expression to be the sum of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
Expr &operator+=(Expr &a, Expr b);

/** Return the difference of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
Expr operator-(Expr a, Expr b);

/** Subtracts a constant integer from an expression. Coerces the type of the
 * integer to match the type of the expression. Errors if the integer
 * cannot be represented in the type of the expression. */
Expr operator-(Expr a, int b);

/** Subtracts an expression from a constant integer. Coerces the type
 * of the integer to match the type of the expression. Errors if the
 * integer cannot be represented in the type of the expression. */
Expr operator-(int a, Expr b);

/** Return the negative of the argument. Does no type casting, so more
 * formally: return that number which when added to the original,
 * yields zero of the same type. For unsigned integers the negative is
 * still an unsigned integer. E.g. in UInt(8), the negative of 56 is
 * 200, because 56 + 200 == 0 */
Expr operator-(Expr a);

/** Modify the first expression to be the difference of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
Expr &operator-=(Expr &a, Expr b);

/** Return the product of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types */
Expr operator*(Expr a, Expr b);

/** Multiply an expression and a constant integer. Coerces the type of the
 * integer to match the type of the expression. Errors if the integer
 * cannot be represented in the type of the expression. */
Expr operator*(Expr a, int b);

/** Multiply a constant integer and an expression. Coerces the type of
 * the integer to match the type of the expression. Errors if the
 * integer cannot be represented in the type of the expression. */
Expr operator*(int a, Expr b);

/** Modify the first expression to be the product of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. */
Expr &operator*=(Expr &a, Expr b);

/** Return the ratio of two expressions, doing any necessary type
 * coercion using \ref Internal::match_types. Note that integer
 * division in Halide is not the same as integer division in C-like
 * languages in two ways.
 *
 * First, signed integer division in Halide rounds according to the
 * sign of the denominator. This means towards minus infinity for
 * positive denominators, and towards positive infinity for negative
 * denominators. This is unlike C, which rounds towards zero. This
 * decision ensures that upsampling expressions like f(x/2, y/2) don't
 * have funny discontinuities when x and y cross zero.
 *
 * Second, division by zero returns zero instead of faulting. For
 * types where overflow is defined behavior, division of the largest
 * negative signed integer by -1 returns the larged negative signed
 * integer for the type (i.e. it wraps). This ensures that a division
 * operation can never have a side-effect, which is helpful in Halide
 * because scheduling directives can expand the domain of computation
 * of a Func, potentially introducing new zero-division.
 */
Expr operator/(Expr a, Expr b);

/** Modify the first expression to be the ratio of two expressions,
 * without changing its type. This casts the second argument to match
 * the type of the first. Note that signed integer division in Halide
 * rounds towards minus infinity, unlike C, which rounds towards
 * zero. */
Expr &operator/=(Expr &a, Expr b);

/** Divides an expression by a constant integer. Coerces the type
 * of the integer to match the type of the expression. Errors if the
 * integer cannot be represented in the type of the expression. */
Expr operator/(Expr a, int b);

/** Divides a constant integer by an expression. Coerces the type
 * of the integer to match the type of the expression. Errors if the
 * integer cannot be represented in the type of the expression. */
Expr operator/(int a, Expr b);

/** Return the first argument reduced modulo the second, doing any
 * necessary type coercion using \ref Internal::match_types. There are
 * two key differences between C-like languages and Halide for the
 * modulo operation, which complement the way division works.
 *
 * First, the result is never negative, so x % 2 is always zero or
 * one, unlike in C-like languages. x % -2 is equivalent, and is also
 * always zero or one. Second, mod by zero evaluates to zero (unlike
 * in C, where it faults). This makes modulo, like division, a
 * side-effect-free operation. */
Expr operator%(Expr a, Expr b);

/** Mods an expression by a constant integer. Coerces the type
 * of the integer to match the type of the expression. Errors if the
 * integer cannot be represented in the type of the expression. */
Expr operator%(Expr a, int b);

/** Mods a constant integer by an expression. Coerces the type
 * of the integer to match the type of the expression. Errors if the
 * integer cannot be represented in the type of the expression. */
Expr operator%(int a, Expr b);

/** Return a boolean expression that tests whether the first argument
 * is greater than the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
Expr operator>(Expr a, Expr b);

/** Return a boolean expression that tests whether an expression is
 * greater than a constant integer. Coerces the integer to the type of
 * the expression. Errors if the integer is not representable in that
 * type. */
Expr operator>(Expr a, int b);

/** Return a boolean expression that tests whether a constant integer is
 * greater than an expression. Coerces the integer to the type of
 * the expression. Errors if the integer is not representable in that
 * type. */
Expr operator>(int a, Expr b);

/** Return a boolean expression that tests whether the first argument
 * is less than the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
Expr operator<(Expr a, Expr b);

/** Return a boolean expression that tests whether an expression is
 * less than a constant integer. Coerces the integer to the type of
 * the expression. Errors if the integer is not representable in that
 * type. */
Expr operator<(Expr a, int b);

/** Return a boolean expression that tests whether a constant integer is
 * less than an expression. Coerces the integer to the type of
 * the expression. Errors if the integer is not representable in that
 * type. */
Expr operator<(int a, Expr b);

/** Return a boolean expression that tests whether the first argument
 * is less than or equal to the second, after doing any necessary type
 * coercion using \ref Internal::match_types */
Expr operator<=(Expr a, Expr b);

/** Return a boolean expression that tests whether an expression is
 * less than or equal to a constant integer. Coerces the integer to
 * the type of the expression. Errors if the integer is not
 * representable in that type. */
Expr operator<=(Expr a, int b);

/** Return a boolean expression that tests whether a constant integer
 * is less than or equal to an expression. Coerces the integer to the
 * type of the expression. Errors if the integer is not representable
 * in that type. */
Expr operator<=(int a, Expr b);

/** Return a boolean expression that tests whether the first argument
 * is greater than or equal to the second, after doing any necessary
 * type coercion using \ref Internal::match_types */
Expr operator>=(Expr a, Expr b);

/** Return a boolean expression that tests whether an expression is
 * greater than or equal to a constant integer. Coerces the integer to
 * the type of the expression. Errors if the integer is not
 * representable in that type. */
Expr operator>=(const Expr &a, int b);

/** Return a boolean expression that tests whether a constant integer
 * is greater than or equal to an expression. Coerces the integer to the
 * type of the expression. Errors if the integer is not representable
 * in that type. */
Expr operator>=(int a, const Expr &b);

/** Return a boolean expression that tests whether the first argument
 * is equal to the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
Expr operator==(Expr a, Expr b);

/** Return a boolean expression that tests whether an expression is
 * equal to a constant integer. Coerces the integer to the type of the
 * expression. Errors if the integer is not representable in that
 * type. */
Expr operator==(Expr a, int b);

/** Return a boolean expression that tests whether a constant integer
 * is equal to an expression. Coerces the integer to the type of the
 * expression. Errors if the integer is not representable in that
 * type. */
Expr operator==(int a, Expr b);

/** Return a boolean expression that tests whether the first argument
 * is not equal to the second, after doing any necessary type coercion
 * using \ref Internal::match_types */
Expr operator!=(Expr a, Expr b);

/** Return a boolean expression that tests whether an expression is
 * not equal to a constant integer. Coerces the integer to the type of
 * the expression. Errors if the integer is not representable in that
 * type. */
Expr operator!=(Expr a, int b);

/** Return a boolean expression that tests whether a constant integer
 * is not equal to an expression. Coerces the integer to the type of
 * the expression. Errors if the integer is not representable in that
 * type. */
Expr operator!=(int a, Expr b);

/** Returns the logical and of the two arguments */
Expr operator&&(Expr a, Expr b);

/** Logical and of an Expr and a bool. Either returns the Expr or an
 * Expr representing false, depending on the bool. */
// @{
Expr operator&&(Expr a, bool b);
Expr operator&&(bool a, Expr b);
// @}

/** Returns the logical or of the two arguments */
Expr operator||(Expr a, Expr b);

/** Logical or of an Expr and a bool. Either returns the Expr or an
 * Expr representing true, depending on the bool. */
// @{
Expr operator||(Expr a, bool b);
Expr operator||(bool a, Expr b);
// @}

/** Returns the logical not the argument */
Expr operator!(Expr a);

/** Returns an expression representing the greater of the two
 * arguments, after doing any necessary type coercion using
 * \ref Internal::match_types. Vectorizes cleanly on most platforms
 * (with the exception of integer types on x86 without SSE4). */
Expr max(Expr a, Expr b);

/** Returns an expression representing the greater of an expression
 * and a constant integer.  The integer is coerced to the type of the
 * expression. Errors if the integer is not representable as that
 * type. Vectorizes cleanly on most platforms (with the exception of
 * integer types on x86 without SSE4). */
Expr max(Expr a, int b);

/** Returns an expression representing the greater of a constant
 * integer and an expression. The integer is coerced to the type of
 * the expression. Errors if the integer is not representable as that
 * type. Vectorizes cleanly on most platforms (with the exception of
 * integer types on x86 without SSE4). */
Expr max(int a, Expr b);

inline Expr max(float a, Expr b) {
    return max(Expr(a), std::move(b));
}
inline Expr max(Expr a, float b) {
    return max(std::move(a), Expr(b));
}

/** Returns an expression representing the greater of an expressions
 * vector, after doing any necessary type coersion using
 * \ref Internal::match_types. Vectorizes cleanly on most platforms
 * (with the exception of integer types on x86 without SSE4).
 * The expressions are folded from right ie. max(.., max(.., ..)).
 * The arguments can be any mix of types but must all be convertible to Expr. */
template<typename A, typename B, typename C, typename... Rest,
         typename std::enable_if<Halide::Internal::all_are_convertible<Expr, Rest...>::value>::type * = nullptr>
inline Expr max(A &&a, B &&b, C &&c, Rest &&...rest) {
    return max(std::forward<A>(a), max(std::forward<B>(b), std::forward<C>(c), std::forward<Rest>(rest)...));
}

Expr min(Expr a, Expr b);

/** Returns an expression representing the lesser of an expression
 * and a constant integer.  The integer is coerced to the type of the
 * expression. Errors if the integer is not representable as that
 * type. Vectorizes cleanly on most platforms (with the exception of
 * integer types on x86 without SSE4). */
Expr min(Expr a, int b);

/** Returns an expression representing the lesser of a constant
 * integer and an expression. The integer is coerced to the type of
 * the expression. Errors if the integer is not representable as that
 * type. Vectorizes cleanly on most platforms (with the exception of
 * integer types on x86 without SSE4). */
Expr min(int a, Expr b);

inline Expr min(float a, Expr b) {
    return min(Expr(a), std::move(b));
}
inline Expr min(Expr a, float b) {
    return min(std::move(a), Expr(b));
}

/** Returns an expression representing the lesser of an expressions
 * vector, after doing any necessary type coersion using
 * \ref Internal::match_types. Vectorizes cleanly on most platforms
 * (with the exception of integer types on x86 without SSE4).
 * The expressions are folded from right ie. min(.., min(.., ..)).
 * The arguments can be any mix of types but must all be convertible to Expr. */
template<typename A, typename B, typename C, typename... Rest,
         typename std::enable_if<Halide::Internal::all_are_convertible<Expr, Rest...>::value>::type * = nullptr>
inline Expr min(A &&a, B &&b, C &&c, Rest &&...rest) {
    return min(std::forward<A>(a), min(std::forward<B>(b), std::forward<C>(c), std::forward<Rest>(rest)...));
}

/** Operators on floats treats those floats as Exprs. Making these
 * explicit prevents implicit float->int casts that might otherwise
 * occur. */
// @{
inline Expr operator+(Expr a, float b) {
    return std::move(a) + Expr(b);
}
inline Expr operator+(float a, Expr b) {
    return Expr(a) + std::move(b);
}
inline Expr operator-(Expr a, float b) {
    return std::move(a) - Expr(b);
}
inline Expr operator-(float a, Expr b) {
    return Expr(a) - std::move(b);
}
inline Expr operator*(Expr a, float b) {
    return std::move(a) * Expr(b);
}
inline Expr operator*(float a, Expr b) {
    return Expr(a) * std::move(b);
}
inline Expr operator/(Expr a, float b) {
    return std::move(a) / Expr(b);
}
inline Expr operator/(float a, Expr b) {
    return Expr(a) / std::move(b);
}
inline Expr operator%(Expr a, float b) {
    return std::move(a) % Expr(b);
}
inline Expr operator%(float a, Expr b) {
    return Expr(a) % std::move(b);
}
inline Expr operator>(Expr a, float b) {
    return std::move(a) > Expr(b);
}
inline Expr operator>(float a, Expr b) {
    return Expr(a) > std::move(b);
}
inline Expr operator<(Expr a, float b) {
    return std::move(a) < Expr(b);
}
inline Expr operator<(float a, Expr b) {
    return Expr(a) < std::move(b);
}
inline Expr operator>=(Expr a, float b) {
    return std::move(a) >= Expr(b);
}
inline Expr operator>=(float a, Expr b) {
    return Expr(a) >= std::move(b);
}
inline Expr operator<=(Expr a, float b) {
    return std::move(a) <= Expr(b);
}
inline Expr operator<=(float a, Expr b) {
    return Expr(a) <= std::move(b);
}
inline Expr operator==(Expr a, float b) {
    return std::move(a) == Expr(b);
}
inline Expr operator==(float a, Expr b) {
    return Expr(a) == std::move(b);
}
inline Expr operator!=(Expr a, float b) {
    return std::move(a) != Expr(b);
}
inline Expr operator!=(float a, Expr b) {
    return Expr(a) != std::move(b);
}
// @}

/** Clamps an expression to lie within the given bounds. The bounds
 * are type-cast to match the expression. Vectorizes as well as min/max. */
Expr clamp(Expr a, const Expr &min_val, const Expr &max_val);

/** Returns the absolute value of a signed integer or floating-point
 * expression. Vectorizes cleanly. Unlike in C, abs of a signed
 * integer returns an unsigned integer of the same bit width. This
 * means that abs of the most negative integer doesn't overflow. */
Expr abs(Expr a);

/** Return the absolute difference between two values. Vectorizes
 * cleanly. Returns an unsigned value of the same bit width. There are
 * various ways to write this yourself, but they contain numerous
 * gotchas and don't always compile to good code, so use this
 * instead. */
Expr absd(Expr a, Expr b);

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
         typename std::enable_if<Halide::Internal::all_are_convertible<Expr, Args...>::value>::type * = nullptr>
inline Expr select(Expr c0, Expr v0, Expr c1, Expr v1, Args &&...args) {
    return select(std::move(c0), std::move(v0), select(std::move(c1), std::move(v1), std::forward<Args>(args)...));
}

/** Equivalent of ternary select(), but taking/returning tuples. If the condition is
 * a Tuple, it must match the size of the true and false Tuples. */
// @{
Tuple select(const Tuple &condition, const Tuple &true_value, const Tuple &false_value);
Tuple select(const Expr &condition, const Tuple &true_value, const Tuple &false_value);
// @}

/** Equivalent of multiway select(), but taking/returning tuples. If the condition is
 * a Tuple, it must match the size of the true and false Tuples. */
// @{
template<typename... Args>
inline Tuple select(const Tuple &c0, const Tuple &v0, const Tuple &c1, const Tuple &v1, Args &&...args) {
    return select(c0, v0, select(c1, v1, std::forward<Args>(args)...));
}
template<typename... Args>
inline Tuple select(const Expr &c0, const Tuple &v0, const Expr &c1, const Tuple &v1, Args &&...args) {
    return select(c0, v0, select(c1, v1, std::forward<Args>(args)...));
}
// @}

/** select applied to FuncRefs (e.g. select(x < 100, f(x), g(x))) is assumed to
 * return an Expr. A runtime error is produced if this is applied to
 * tuple-valued Funcs. In that case you should explicitly cast the second and
 * third args to Tuple to remove the ambiguity. */
// @{
Expr select(const Expr &condition, const FuncRef &true_value, const FuncRef &false_value);
template<typename... Args>
inline Expr select(const Expr &c0, const FuncRef &v0, const Expr &c1, const FuncRef &v1, Args &&...args) {
    return select(c0, v0, select(c1, v1, std::forward<Args>(args)...));
}
// @}

/** Oftentimes we want to pack a list of expressions with the same type
 * into a channel dimension, e.g.,
 * img(x, y, c) = select(c == 0, 100, // Red
 *                       c == 1, 50,  // Green
 *                               25); // Blue
 * This is tedious when the list is long. The following function
 * provide convinent syntax that allow one to write:
 * img(x, y, c) = mux(c, {100, 50, 25});
 *
 * As with the select equivalent, if the first argument (the index) is
 * out of range, the expression evaluates to the last value.
 */
// @{
Expr mux(const Expr &id, const std::initializer_list<Expr> &values);
Expr mux(const Expr &id, const std::vector<Expr> &values);
Expr mux(const Expr &id, const Tuple &values);
Expr mux(const Expr &id, const std::initializer_list<FuncRef> &values);
Tuple mux(const Expr &id, const std::initializer_list<Tuple> &values);
Tuple mux(const Expr &id, const std::vector<Tuple> &values);
// @}

/** Return the sine of a floating-point expression. If the argument is
 * not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
Expr sin(Expr x);

/** Return the arcsine of a floating-point expression. If the argument
 * is not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
Expr asin(Expr x);

/** Return the cosine of a floating-point expression. If the argument
 * is not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
Expr cos(Expr x);

/** Return the arccosine of a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). Does not
 * vectorize well. */
Expr acos(Expr x);

/** Return the tangent of a floating-point expression. If the argument
 * is not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
Expr tan(Expr x);

/** Return the arctangent of a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). Does not
 * vectorize well. */
Expr atan(Expr x);

/** Return the angle of a floating-point gradient. If the argument is
 * not floating-point, it is cast to Float(32). Does not vectorize
 * well. */
Expr atan2(Expr y, Expr x);

/** Return the hyperbolic sine of a floating-point expression.  If the
 *  argument is not floating-point, it is cast to Float(32). Does not
 *  vectorize well. */
Expr sinh(Expr x);

/** Return the hyperbolic arcsinhe of a floating-point expression.  If
 * the argument is not floating-point, it is cast to Float(32). Does
 * not vectorize well. */
Expr asinh(Expr x);

/** Return the hyperbolic cosine of a floating-point expression.  If
 * the argument is not floating-point, it is cast to Float(32). Does
 * not vectorize well. */
Expr cosh(Expr x);

/** Return the hyperbolic arccosine of a floating-point expression.
 * If the argument is not floating-point, it is cast to
 * Float(32). Does not vectorize well. */
Expr acosh(Expr x);

/** Return the hyperbolic tangent of a floating-point expression.  If
 * the argument is not floating-point, it is cast to Float(32). Does
 * not vectorize well. */
Expr tanh(Expr x);

/** Return the hyperbolic arctangent of a floating-point expression.
 * If the argument is not floating-point, it is cast to
 * Float(32). Does not vectorize well. */
Expr atanh(Expr x);

/** Return the square root of a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). Typically
 * vectorizes cleanly. */
Expr sqrt(Expr x);

/** Return the square root of the sum of the squares of two
 * floating-point expressions. If the argument is not floating-point,
 * it is cast to Float(32). Vectorizes cleanly. */
Expr hypot(const Expr &x, const Expr &y);

/** Return the exponential of a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). For
 * Float(64) arguments, this calls the system exp function, and does
 * not vectorize well. For Float(32) arguments, this function is
 * vectorizable, does the right thing for extremely small or extremely
 * large inputs, and is accurate up to the last bit of the
 * mantissa. Vectorizes cleanly. */
Expr exp(Expr x);

/** Return the logarithm of a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). For
 * Float(64) arguments, this calls the system log function, and does
 * not vectorize well. For Float(32) arguments, this function is
 * vectorizable, does the right thing for inputs <= 0 (returns -inf or
 * nan), and is accurate up to the last bit of the
 * mantissa. Vectorizes cleanly. */
Expr log(Expr x);

/** Return one floating point expression raised to the power of
 * another. The type of the result is given by the type of the first
 * argument. If the first argument is not a floating-point type, it is
 * cast to Float(32). For Float(32), cleanly vectorizable, and
 * accurate up to the last few bits of the mantissa. Gets worse when
 * approaching overflow. Vectorizes cleanly. */
Expr pow(Expr x, Expr y);

/** Evaluate the error function erf. Only available for
 * Float(32). Accurate up to the last three bits of the
 * mantissa. Vectorizes cleanly. */
Expr erf(const Expr &x);

/** Struct that allows the user to specify several requirements for functions
 * that are approximated by polynomial expansions. These polynomials can be
 * optimized for four different metrics: Mean Squared Error, Maximum Absolute Error,
 * Maximum Units in Last Place (ULP) Error, or a 50%/50% blend of MAE and MULPE.
 *
 * Orthogonally to the optimization objective, these polynomials can vary
 * in degree. Higher degree polynomials will give more precise results.
 * Note that instead of specifying the degree, the number of terms is used instead.
 * E.g., even (i.e., symmetric) functions may be implemented using only even powers,
 * for which a number of terms of 4 would actually mean that terms
 * in [1, x^2, x^4, x^6] are used, which is degree 6.
 *
 * Additionally, if you don't care about number of terms in the polynomial
 * and you do care about the maximal absolute error the approximation may have
 * over the domain, you may specify values and the implementation
 * will decide the appropriate polynomial degree that achieves this precision.
 */
struct ApproximationPrecision {
    enum OptimizationObjective {
        MSE,        //< Mean Squared Error Optimized.
        MAE,        //< Optimized for Max Absolute Error.
        MULPE,      //< Optimized for Max ULP Error. ULP is "Units in Last Place", measured in IEEE 32-bit floats.
        MULPE_MAE,  //< Optimized for simultaneously Max ULP Error, and Max Absolute Error, each with a weight of 50%.
    } optimized_for;
    int constraint_min_poly_terms{0};           //< Number of terms in polynomial (zero for no constraint).
    float constraint_max_absolute_error{0.0f};  //< Max absolute error (zero for no constraint).
    bool allow_native_when_faster{true};        //< For some targets, the native functions are really fast.
                                                //  Put this on false to force expansion of the polynomial approximation.
};

/** Fast vectorizable approximation to some trigonometric functions for
 * Float(32).  Absolute approximation error is less than 1e-5. Slow on x86 if
 * you don't have at least sse 4.1. */
// @{
Expr fast_sin(const Expr &x, ApproximationPrecision precision = {ApproximationPrecision::MULPE, 0, 1e-5});
Expr fast_cos(const Expr &x, ApproximationPrecision precision = {ApproximationPrecision::MULPE, 0, 1e-5});
// @}


/** Fast vectorizable approximations for arctan and arctan2 for Float(32).
 *
 * Desired precision can be specified as either a maximum absolute error (MAE) or
 * the number of terms in the polynomial approximation (see the ApproximationPrecision enum) which
 * are optimized for either:
 *  - MSE (Mean Squared Error)
 *  - MAE (Maximum Absolute Error)
 *  - MULPE (Maximum Units in Last Place Error).
 *
 * The default (Max ULP Error Polynomial of 6 terms) has a MAE of 3.53e-6.
 * For more info on the available approximations and their precisions, see the table in ApproximationTables.cpp.
 *
 * Note: the polynomial uses odd powers, so the number of terms is not the degree of the polynomial.
 * Note: the polynomial with 8 terms is only useful to increase precision for fast_atan, and not for fast_atan2.
 * Note: the performance of this functions seem to be not reliably faster on WebGPU (for now, August 2024).
 */
// @{
Expr fast_atan(const Expr &x, ApproximationPrecision precision = {ApproximationPrecision::MULPE, 0, 1e-5});
Expr fast_atan2(const Expr &y, const Expr &x, ApproximationPrecision = {ApproximationPrecision::MULPE, 0, 1e-5});
// @}

/** Fast approximate cleanly vectorizable log for Float(32). Returns
 * nonsense for x <= 0.0f. Accurate up to the last 5 bits of the
 * mantissa. Vectorizes cleanly. Slow on x86 if you don't
 * have at least sse 4.1. */
Expr fast_log(const Expr &x, ApproximationPrecision precision = {ApproximationPrecision::MULPE, 0, 1e-5});

/** Fast approximate cleanly vectorizable exp for Float(32). Returns
 * nonsense for inputs that would overflow or underflow. Typically
 * accurate up to the last 5 bits of the mantissa. Gets worse when
 * approaching overflow. Vectorizes cleanly. Slow on x86 if you don't
 * have at least sse 4.1. */
Expr fast_exp(const Expr &x, ApproximationPrecision precision = {ApproximationPrecision::MULPE, 0, 1e-5});

/** Fast approximate cleanly vectorizable pow for Float(32). Returns
 * nonsense for x < 0.0f. Accurate up to the last 5 bits of the
 * mantissa for typical exponents. Gets worse when approaching
 * overflow. Vectorizes cleanly. Slow on x86 if you don't
 * have at least sse 4.1. */
Expr fast_pow(Expr x, Expr y, ApproximationPrecision precision = {ApproximationPrecision::MULPE, 0, 1e-5});

/** Fast approximate inverse for Float(32). Corresponds to the rcpps
 * instruction on x86, and the vrecpe instruction on ARM. Vectorizes
 * cleanly. Note that this can produce slightly different results
 * across different implementations of the same architecture (e.g. AMD vs Intel),
 * even when strict_float is enabled. */
Expr fast_inverse(Expr x);

/** Fast approximate inverse square root for Float(32). Corresponds to
 * the rsqrtps instruction on x86, and the vrsqrte instruction on
 * ARM. Vectorizes cleanly. Note that this can produce slightly different results
 * across different implementations of the same architecture (e.g. AMD vs Intel),
 * even when strict_float is enabled. */
Expr fast_inverse_sqrt(Expr x);

/** Return the greatest whole number less than or equal to a
 * floating-point expression. If the argument is not floating-point,
 * it is cast to Float(32). The return value is still in floating
 * point, despite being a whole number. Vectorizes cleanly. */
Expr floor(Expr x);

/** Return the least whole number greater than or equal to a
 * floating-point expression. If the argument is not floating-point,
 * it is cast to Float(32). The return value is still in floating
 * point, despite being a whole number. Vectorizes cleanly. */
Expr ceil(Expr x);

/** Return the whole number closest to a floating-point expression. If the
 * argument is not floating-point, it is cast to Float(32). The return value is
 * still in floating point, despite being a whole number. On ties, we round
 * towards the nearest even integer. Note that this is not the same as
 * std::round in C, which rounds away from zero. On platforms without a native
 * instruction for this, it is emulated, and may be more expensive than
 * cast<int>(x + 0.5f) or similar. */
Expr round(Expr x);

/** Return the integer part of a floating-point expression. If the argument is
 * not floating-point, it is cast to Float(32). The return value is still in
 * floating point, despite being a whole number. Vectorizes cleanly. */
Expr trunc(Expr x);

/** Returns true if the argument is a Not a Number (NaN). Requires a
 * floating point argument.  Vectorizes cleanly.
 * Note that the Expr passed in will be evaluated in strict_float mode,
 * regardless of whether strict_float mode is enabled in the current Target. */
Expr is_nan(Expr x);

/** Returns true if the argument is Inf or -Inf. Requires a
 * floating point argument.  Vectorizes cleanly.
 * Note that the Expr passed in will be evaluated in strict_float mode,
 * regardless of whether strict_float mode is enabled in the current Target. */
Expr is_inf(Expr x);

/** Returns true if the argument is a finite value (ie, neither NaN nor Inf).
 * Requires a floating point argument.  Vectorizes cleanly.
 * Note that the Expr passed in will be evaluated in strict_float mode,
 * regardless of whether strict_float mode is enabled in the current Target. */
Expr is_finite(Expr x);

/** Return the fractional part of a floating-point expression. If the argument
 *  is not floating-point, it is cast to Float(32). The return value has the
 *  same sign as the original expression. Vectorizes cleanly. */
Expr fract(const Expr &x);

/** Reinterpret the bits of one value as another type. */
Expr reinterpret(Type t, Expr e);

template<typename T>
Expr reinterpret(Expr e) {
    return reinterpret(type_of<T>(), std::move(e));
}

/** Return the bitwise and of two expressions (which need not have the
 * same type).  The result type is the wider of the two expressions.
 * Only integral types are allowed and both expressions must be signed
 * or both must be unsigned. */
Expr operator&(Expr x, Expr y);

/** Return the bitwise and of an expression and an integer. The type
 * of the result is the type of the expression argument. */
// @{
Expr operator&(Expr x, int y);
Expr operator&(int x, Expr y);
// @}

/** Return the bitwise or of two expressions (which need not have the
 * same type).  The result type is the wider of the two expressions.
 * Only integral types are allowed and both expressions must be signed
 * or both must be unsigned. */
Expr operator|(Expr x, Expr y);

/** Return the bitwise or of an expression and an integer. The type of
 * the result is the type of the expression argument. */
// @{
Expr operator|(Expr x, int y);
Expr operator|(int x, Expr y);
// @}

/** Return the bitwise xor of two expressions (which need not have the
 * same type).  The result type is the wider of the two expressions.
 * Only integral types are allowed and both expressions must be signed
 * or both must be unsigned. */
Expr operator^(Expr x, Expr y);

/** Return the bitwise xor of an expression and an integer. The type
 * of the result is the type of the expression argument. */
// @{
Expr operator^(Expr x, int y);
Expr operator^(int x, Expr y);
// @}

/** Return the bitwise not of an expression. */
Expr operator~(Expr x);

/** Shift the bits of an integer value left. This is actually less
 * efficient than multiplying by 2^n, because Halide's optimization
 * passes understand multiplication, and will compile it to
 * shifting. This operator is only for if you really really need bit
 * shifting (e.g. because the exponent is a run-time parameter). The
 * type of the result is equal to the type of the first argument. Both
 * arguments must have integer type. */
// @{
Expr operator<<(Expr x, Expr y);
Expr operator<<(Expr x, int y);
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
Expr operator>>(Expr x, Expr y);
Expr operator>>(Expr x, int y);
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
Expr lerp(Expr zero_val, Expr one_val, Expr weight);

/** Count the number of set bits in an expression. */
Expr popcount(Expr x);

/** Count the number of leading zero bits in an expression. If the expression is
 * zero, the result is the number of bits in the type. */
Expr count_leading_zeros(Expr x);

/** Count the number of trailing zero bits in an expression. If the expression is
 * zero, the result is the number of bits in the type. */
Expr count_trailing_zeros(Expr x);

/** Divide two integers, rounding towards zero. This is the typical
 * behavior of most hardware architectures, which differs from
 * Halide's division operator, which is Euclidean (rounds towards
 * -infinity). Will throw a runtime error if y is zero, or if y is -1
 * and x is the minimum signed integer. */
Expr div_round_to_zero(Expr x, Expr y);

/** Compute the remainder of dividing two integers, when division is
 * rounding toward zero. This is the typical behavior of most hardware
 * architectures, which differs from Halide's mod operator, which is
 * Euclidean (produces the remainder when division rounds towards
 * -infinity). Will throw a runtime error if y is zero. */
Expr mod_round_to_zero(Expr x, Expr y);

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
Expr random_float(Expr seed = Expr());

/** Return a random variable representing a uniformly distributed
 * unsigned 32-bit integer. See \ref random_float. Vectorizes cleanly. */
Expr random_uint(Expr seed = Expr());

/** Return a random variable representing a uniformly distributed
 * 32-bit integer. See \ref random_float. Vectorizes cleanly. */
Expr random_int(Expr seed = Expr());

/** Create an Expr that prints out its value whenever it is
 * evaluated. It also prints out everything else in the arguments
 * list, separated by spaces. This can include string literals. */
//@{
Expr print(const std::vector<Expr> &values);

template<typename... Args>
inline HALIDE_NO_USER_CODE_INLINE Expr print(Expr a, Args &&...args) {
    std::vector<Expr> collected_args = {std::move(a)};
    Internal::collect_print_args(collected_args, std::forward<Args>(args)...);
    return print(collected_args);
}
//@}

/** Create an Expr that prints whenever it is evaluated, provided that
 * the condition is true. */
// @{
Expr print_when(Expr condition, const std::vector<Expr> &values);

template<typename... Args>
inline HALIDE_NO_USER_CODE_INLINE Expr print_when(Expr condition, Expr a, Args &&...args) {
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

template<typename... Args>
inline HALIDE_NO_USER_CODE_INLINE Expr require(Expr condition, Expr value, Args &&...args) {
    std::vector<Expr> collected_args = {std::move(value)};
    Internal::collect_print_args(collected_args, std::forward<Args>(args)...);
    return require(std::move(condition), collected_args);
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
Expr undef(Type t);

template<typename T>
inline Expr undef() {
    return undef(type_of<T>());
}

namespace Internal {

/** Return an expression that should never be evaluated. Expressions
 * that depend on unreachabale values are also unreachable, and
 * statements that execute unreachable expressions are also considered
 * unreachable. */
Expr unreachable(Type t = Int(32));

template<typename T>
inline Expr unreachable() {
    return unreachable(type_of<T>());
}

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
template<typename... Args>
inline HALIDE_NO_USER_CODE_INLINE Expr memoize_tag(Expr result, Args &&...args) {
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
Expr likely(Expr e);

/** Equivalent to likely, but only triggers a loop partitioning if
 * found in an innermost loop. */
Expr likely_if_innermost(Expr e);

/** Cast an expression to the halide type corresponding to the C++
 * type T. As part of the cast, clamp to the minimum and maximum
 * values of the result type. */
template<typename T>
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
Expr strict_float(Expr e);

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
Expr unsafe_promise_clamped(const Expr &value, const Expr &min, const Expr &max);

namespace Internal {
/**
 * FOR INTERNAL USE ONLY.
 *
 * An entirely unchecked version of unsafe_promise_clamped, used
 * inside the compiler as an annotation of the known bounds of an Expr
 * when it has proved something is bounded and wants to record that
 * fact for later passes (notably bounds inference) to exploit. This
 * gets introduced by GuardWithIf tail strategies, because the bounds
 * machinery has a hard time exploiting if statement conditions.
 *
 * Unlike unsafe_promise_clamped, this expression is
 * context-dependent, because 'value' might be statically bounded at
 * some point in the IR (e.g. due to a containing if statement), but
 * not elsewhere.
 *
 * This intrinsic always evaluates to its first argument. If this value is
 * used by a side-effecting operation and it is outside the range specified
 * by its second and third arguments, behavior is undefined. The compiler can
 * therefore assume that the value is within the range given and optimize
 * accordingly. Note that this permits promise_clamped to evaluate to
 * something outside of the range, provided that this value is not used.
 *
 * Note that this produces an intrinsic that is marked as 'pure' and thus is
 * allowed to be hoisted, etc.; thus, extra care must be taken with its use.
 **/
Expr promise_clamped(const Expr &value, const Expr &min, const Expr &max);
}  // namespace Internal

/** Scatter and gather are used for update definition which must store
 * multiple values to distinct locations at the same time. The
 * multiple expressions on the right-hand-side are bundled together
 * into a "gather", which must match a "scatter" the the same number
 * of arguments on the left-hand-size. For example, to store the
 * values 1 and 2 to the locations (x, y, 3) and (x, y, 4),
 * respectively:
 *
\code
f(x, y, scatter(3, 4)) = gather(1, 2);
\endcode
 *
 * The result of gather or scatter can be treated as an
 * expression. Any containing operations on it can be assumed to
 * distribute over the elements. If two gather expressions are
 * combined with an arithmetic operator (e.g. added), they combine
 * element-wise. The following example stores the values 2 * x, 2 * y,
 * and 2 * c to the locations (x + 1, y, c), (x, y + 3, c), and (x, y,
 * c + 2) respectively:
 *
\code
f(x + scatter(1, 0, 0), y + scatter(0, 3, 0), c + scatter(0, 0, 2)) = 2 * gather(x, y, c);
\endcode
*
* Repeated values in the scatter cause multiple stores to the same
* location. The stores happen in order from left to right, so the
* rightmost value wins. The following code is equivalent to f(x) = 5
*
\code
f(scatter(x, x)) = gather(3, 5);
\endcode
*
* Gathers are most useful for algorithms which require in-place
* swapping or permutation of multiple elements, or other kinds of
* in-place mutations that require loading multiple inputs, doing some
* operations to them jointly, then storing them again. The following
* update definition swaps the values of f at locations 3 and 5 if an
* input parameter p is true:
*
\code
f(scatter(3, 5)) = f(select(p, gather(5, 3), gather(3, 5)));
\endcode
*
* For more examples of the use of scatter and gather, see
* test/correctness/multiple_scatter.cpp
*
* It is not currently possible to use scatter and gather to write an
* update definition in which the *number* of values loaded or stored
* varies, as the size of the scatter/gather packet must be fixed a
* compile-time. A workaround is to make the unwanted extra operations
* a redundant copy of the last operation, which will be
* dead-code-eliminated by the compiler. For example, the following
* update definition swaps the values at locations 3 and 5 when the
* parameter p is true, and rotates the values at locations 1, 2, and 3
* when it is false. The load from 3 and store to 5 will be redundantly
* repeated:
*
\code
f(select(p, scatter(3, 5, 5), scatter(1, 2, 3))) = f(select(p, gather(5, 3, 3), gather(2, 3, 1)));
\endcode
*
* Note that in the p == true case, we redudantly load from 3 and write
* to 5 twice.
*/
//@{
Expr scatter(const std::vector<Expr> &args);
Expr gather(const std::vector<Expr> &args);

template<typename... Args>
Expr scatter(const Expr &e, Args &&...args) {
    return scatter({e, std::forward<Args>(args)...});
}

template<typename... Args>
Expr gather(const Expr &e, Args &&...args) {
    return gather({e, std::forward<Args>(args)...});
}
// @}

/** Extract a contiguous subsequence of the bits of 'e', starting at the bit
 * index given by 'lsb', where zero is the least-significant bit, returning a
 * value of type 't'. Any out-of-range bits requested are filled with zeros.
 *
 * extract_bits is especially useful when one wants to load a small vector of a
 * wide type, and treat it as a larger vector of a smaller type. For example,
 * loading a vector of 32 uint8 values from a uint32 Func can be done as
 * follows:
\code
f8(x) = extract_bits<uint8_t>(f32(x/4), 8*(x%4));
f8.align_bounds(x, 4).vectorize(x, 32);
\endcode
 * Note that the align_bounds call is critical so that the narrow Exprs are
 * aligned to the wider Exprs. This makes the x%4 term collapse to a
 * constant. If f8 is an output Func, then constraining the min value of x to be
 * a known multiple of four would also be sufficient, e.g. via:
\code
f8.output_buffer().dim(0).set_min(0);
\endcode
 *
 * See test/correctness/extract_concat_bits.cpp for a complete example. */
// @{
Expr extract_bits(Type t, const Expr &e, const Expr &lsb);

template<typename T>
Expr extract_bits(const Expr &e, const Expr &lsb) {
    return extract_bits(type_of<T>(), e, lsb);
}
// @}

/** Given a number of Exprs of the same type, concatenate their bits producing a
 * single Expr of the same type code of the input but with more bits. The
 * number of arguments must be a power of two.
 *
 * concat_bits is especially useful when one wants to treat a Func containing
 * values of a narrow type as a Func containing fewer values of a wider
 * type. For example, the following code reinterprets vectors of 32 uint8 values
 * as a vector of 8 uint32s:
 *
\code
f32(x) = concat_bits({f8(4*x), f8(4*x + 1), f8(4*x + 2), f8(4*x + 3)});
f32.vectorize(x, 8);
\endcode
 *
 * See test/correctness/extract_concat_bits.cpp for a complete example.
 */
Expr concat_bits(const std::vector<Expr> &e);

/** Below is a collection of intrinsics for fixed-point programming. Most of
 * them can be expressed via other means, but this is more natural for some, as
 * it avoids ghost widened intermediates that don't (or shouldn't) actually show
 * up in codegen, and doesn't rely on pattern-matching inside the compiler to
 * succeed to get good instruction selection.
 *
 * The semantics of each call are defined in terms of a non-existent 'widen' and
 * 'narrow' operators, which stand in for casts that double or halve the
 * bit-width of a type respectively.
 */

/** Compute a + widen(b). */
Expr widen_right_add(Expr a, Expr b);

/** Compute a * widen(b). */
Expr widen_right_mul(Expr a, Expr b);

/** Compute a - widen(b). */
Expr widen_right_sub(Expr a, Expr b);

/** Compute widen(a) + widen(b). */
Expr widening_add(Expr a, Expr b);

/** Compute widen(a) * widen(b). a and b may have different signedness, in which
 * case the result is signed. */
Expr widening_mul(Expr a, Expr b);

/** Compute widen(a) - widen(b). The result is always signed. */
Expr widening_sub(Expr a, Expr b);

/** Compute widen(a) << b. */
//@{
Expr widening_shift_left(Expr a, Expr b);
Expr widening_shift_left(Expr a, int b);
//@}

/** Compute widen(a) >> b. */
//@{
Expr widening_shift_right(Expr a, Expr b);
Expr widening_shift_right(Expr a, int b);
//@}

/** Compute saturating_narrow(widening_add(a, (1 >> min(b, 0)) / 2) << b).
 * When b is positive indicating a left shift, the rounding term is zero. */
//@{
Expr rounding_shift_left(Expr a, Expr b);
Expr rounding_shift_left(Expr a, int b);
//@}

/** Compute saturating_narrow(widening_add(a, (1 << max(b, 0)) / 2) >> b).
 * When b is negative indicating a left shift, the rounding term is zero. */
//@{
Expr rounding_shift_right(Expr a, Expr b);
Expr rounding_shift_right(Expr a, int b);
//@}

/** Compute saturating_narrow(widen(a) + widen(b)) */
Expr saturating_add(Expr a, Expr b);

/** Compute saturating_narrow(widen(a) - widen(b)) */
Expr saturating_sub(Expr a, Expr b);

/** Compute narrow((widen(a) + widen(b)) / 2) */
Expr halving_add(Expr a, Expr b);

/** Compute narrow((widen(a) + widen(b) + 1) / 2) */
Expr rounding_halving_add(Expr a, Expr b);

/** Compute narrow((widen(a) - widen(b)) / 2) */
Expr halving_sub(Expr a, Expr b);

/** Compute saturating_narrow(shift_right(widening_mul(a, b), q)) */
//@{
Expr mul_shift_right(Expr a, Expr b, Expr q);
Expr mul_shift_right(Expr a, Expr b, int q);
//@}

/** Compute saturating_narrow(rounding_shift_right(widening_mul(a, b), q)) */
//@{
Expr rounding_mul_shift_right(Expr a, Expr b, Expr q);
Expr rounding_mul_shift_right(Expr a, Expr b, int q);
//@}

/** Return a boolean Expr for the corresponding field of the Target
 * being used during lowering; they can be useful in writing library
 * code without having to plumb a Target through call sites, so that you
 * can do things like
 \code
    Expr e = select(target_arch_is(Target::ARM), something, something_else);
 \endcode
 * Note that this doesn't do any checking at runtime to verify that the Target
 * is valid for the current hardware configuration.
 */
//@{
Expr target_arch_is(Target::Arch arch);
Expr target_os_is(Target::OS os);
Expr target_has_feature(Target::Feature feat);
//@}

/** Return the bit width of the Target used during lowering; this can be useful
 * in writing library code without having to plumb a Target through call sites,
 * so that you can do things like
 \code
    Expr e = select(target_bits() == 32, something, something_else);
 \endcode
 * Note that this doesn't do any checking at runtime to verify that the Target
 * is valid for the current hardware configuration.
 */
Expr target_bits();

/** Return the natural vector width for the given Type for the Target
 * being used during lowering; this can be useful in writing library
 * code without having to plumb a Target through call sites, so that you
 * can do things like
 \code
    f.vectorize(x, target_natural_vector_size(Float(32)));
 \endcode
 * Note that this doesn't do any checking at runtime to verify that the Target
 * is valid for the current hardware configuration.
 */
//@{
Expr target_natural_vector_size(Type t);
template<typename data_t>
Expr target_natural_vector_size() {
    return target_natural_vector_size(type_of<data_t>());
}
//@}

}  // namespace Halide

#endif

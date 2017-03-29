#include "IROperator.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "Halide.h"

#include <string>

namespace h = Halide;
namespace p = boost::python;

h::Expr reinterpret0(h::Type t, h::Expr e) {
    return h::reinterpret(t, e);
}

h::Expr cast0(h::Type t, h::Expr e) {
    return Halide::cast(t, e);
}

h::Expr select0(h::Expr condition, h::Expr true_value, h::Expr false_value) {
    return h::select(condition, true_value, false_value);
}

h::Expr select1(h::Expr c1, h::Expr v1,
                h::Expr c2, h::Expr v2,
                h::Expr default_val) {
    return h::select(c1, v1,
                     c2, v2, default_val);
}
h::Expr select2(h::Expr c1, h::Expr v1,
                h::Expr c2, h::Expr v2,
                h::Expr c3, h::Expr v3,
                h::Expr default_val) {
    return h::select(c1, v1,
                     c2, v2,
                     c3, v3, default_val);
}
h::Expr select3(h::Expr c1, h::Expr v1,
                h::Expr c2, h::Expr v2,
                h::Expr c3, h::Expr v3,
                h::Expr c4, h::Expr v4,
                h::Expr default_val) {
    return h::select(c1, v1,
                     c2, v2,
                     c3, v3,
                     c4, v4, default_val);
}
h::Expr select4(h::Expr c1, h::Expr v1,
                h::Expr c2, h::Expr v2,
                h::Expr c3, h::Expr v3,
                h::Expr c4, h::Expr v4,
                h::Expr c5, h::Expr v5,
                h::Expr default_val) {
    return h::select(c1, v1,
                     c2, v2,
                     c3, v3,
                     c4, v4,
                     c5, v5, default_val);
}
h::Expr select5(h::Expr c1, h::Expr v1,
                h::Expr c2, h::Expr v2,
                h::Expr c3, h::Expr v3,
                h::Expr c4, h::Expr v4,
                h::Expr c5, h::Expr v5,
                h::Expr c6, h::Expr v6,
                h::Expr default_val) {
    return h::select(c1, v1,
                     c2, v2,
                     c3, v3,
                     c4, v4,
                     c5, v5,
                     c6, v6, default_val);
}
h::Expr select6(h::Expr c1, h::Expr v1,
                h::Expr c2, h::Expr v2,
                h::Expr c3, h::Expr v3,
                h::Expr c4, h::Expr v4,
                h::Expr c5, h::Expr v5,
                h::Expr c6, h::Expr v6,
                h::Expr c7, h::Expr v7,
                h::Expr default_val) {
    return h::select(c1, v1,
                     c2, v2,
                     c3, v3,
                     c4, v4,
                     c5, v5,
                     c6, v6,
                     c7, v7, default_val);
}
h::Expr select7(h::Expr c1, h::Expr v1,
                h::Expr c2, h::Expr v2,
                h::Expr c3, h::Expr v3,
                h::Expr c4, h::Expr v4,
                h::Expr c5, h::Expr v5,
                h::Expr c6, h::Expr v6,
                h::Expr c7, h::Expr v7,
                h::Expr c8, h::Expr v8,
                h::Expr default_val) {
    return h::select(c1, v1,
                     c2, v2,
                     c3, v3,
                     c4, v4,
                     c5, v5,
                     c6, v6,
                     c7, v7,
                     c8, v8, default_val);
}
h::Expr select8(h::Expr c1, h::Expr v1,
                h::Expr c2, h::Expr v2,
                h::Expr c3, h::Expr v3,
                h::Expr c4, h::Expr v4,
                h::Expr c5, h::Expr v5,
                h::Expr c6, h::Expr v6,
                h::Expr c7, h::Expr v7,
                h::Expr c8, h::Expr v8,
                h::Expr c9, h::Expr v9,
                h::Expr default_val) {
    return h::select(c1, v1,
                     c2, v2,
                     c3, v3,
                     c4, v4,
                     c5, v5,
                     c6, v6,
                     c7, v7,
                     c8, v8,
                     c9, v9, default_val);
}
h::Expr select9(h::Expr c1, h::Expr v1,
                h::Expr c2, h::Expr v2,
                h::Expr c3, h::Expr v3,
                h::Expr c4, h::Expr v4,
                h::Expr c5, h::Expr v5,
                h::Expr c6, h::Expr v6,
                h::Expr c7, h::Expr v7,
                h::Expr c8, h::Expr v8,
                h::Expr c9, h::Expr v9,
                h::Expr c10, h::Expr v10,
                h::Expr default_val) {
    return h::select(c1, v1,
                     c2, v2,
                     c3, v3,
                     c4, v4,
                     c5, v5,
                     c6, v6,
                     c7, v7,
                     c8, v8,
                     c9, v9,
                     c10, v10, default_val);
}

h::Expr print_when0(h::Expr condition, p::tuple values_passed) {
    const size_t num_values = p::len(values_passed);
    std::vector<h::Expr> values;
    values.reserve(num_values);

    for (size_t i = 0; i < num_values; i += 1) {
        p::object o = values_passed[i];
        p::extract<h::Expr &> expr_extract(o);

        if (expr_extract.check()) {
            values.push_back(expr_extract());
        } else {
            for (size_t j = 0; j < num_values; j += 1) {
                p::object o = values_passed[j];
                const std::string o_str = p::extract<std::string>(p::str(o));
                printf("print_when values[%lu] == %s\n", j, o_str.c_str());
            }
            throw std::invalid_argument("print_when only handles a list/tuple of (convertible to) Expr.");
        }
    }

    return h::print_when(condition, values);
}

h::Expr random_float0() {
    return h::random_float();
}

h::Expr random_float1(h::Expr seed) {
    return h::random_float(seed);
}

h::Expr random_int0() {
    return h::random_int();
}

h::Expr random_int1(h::Expr seed) {
    return h::random_int(seed);
}

h::Expr undef0(h::Type type) {
    return h::undef(type);
}

h::Expr memoize_tag0(h::Expr result, const std::vector<h::Expr> &cache_key_values) {
    return h::memoize_tag(result, cache_key_values);
}

void defineOperators() {
    // defined in IROperator.h

    h::Expr (*max_exprs)(h::Expr, h::Expr) = &h::max;
    h::Expr (*max_expr_int)(const h::Expr &, int) = &h::max;
    h::Expr (*max_int_expr)(int, const h::Expr &) = &h::max;

    h::Expr (*min_exprs)(h::Expr, h::Expr) = &h::min;
    h::Expr (*min_expr_int)(const h::Expr &, int) = &h::min;
    h::Expr (*min_int_expr)(int, const h::Expr &) = &h::min;

    p::def("max", max_exprs,
           p::args("a", "b"),
           "Returns an expression representing the greater of the two "
           "arguments, after doing any necessary type coercion using "
           "Internal::match_types. Vectorizes cleanly on most platforms "
           "(with the exception of integer types on x86 without SSE4).");

    p::def("max", max_expr_int,
           p::args("a", "b"),
           "Returns an expression representing the greater of an expression"
           " and a constant integer.  The integer is coerced to the type of the"
           " expression. Errors if the integer is not representable as that"
           " type. Vectorizes cleanly on most platforms (with the exception of"
           " integer types on x86 without SSE4).");

    p::def("max", max_int_expr,
           p::args("a", "b"),
           "Returns an expression representing the greater of a constant"
           " integer and an expression. The integer is coerced to the type of"
           " the expression. Errors if the integer is not representable as that"
           " type. Vectorizes cleanly on most platforms (with the exception of"
           " integer types on x86 without SSE4).");

    p::def("min", min_exprs,
           p::args("a", "b"),
           "Returns an expression representing the greater of the two "
           "arguments, after doing any necessary type coercion using "
           "Internal::match_types. Vectorizes cleanly on most platforms "
           "(with the exception of integer types on x86 without SSE4).");

    p::def("min", min_expr_int,
           p::args("a", "b"),
           "Returns an expression representing the lesser of an expression"
           " and a constant integer.  The integer is coerced to the type of the"
           " expression. Errors if the integer is not representable as that"
           " type. Vectorizes cleanly on most platforms (with the exception of"
           " integer types on x86 without SSE4).");

    p::def("min", min_int_expr,
           p::args("a", "b"),
           "Returns an expression representing the lesser of a constant"
           " integer and an expression. The integer is coerced to the type of"
           " the expression. Errors if the integer is not representable as that"
           " type. Vectorizes cleanly on most platforms (with the exception of"
           " integer types on x86 without SSE4).");

    p::def("clamp", &h::clamp,
           p::args("a", "min_val", "max_val"),
           "Clamps an expression to lie within the given bounds. The bounds "
           "are type-cast to match the expression. Vectorizes as well as min/max.");

    p::def("abs", &h::abs, p::args("a"),
           "Returns the absolute value of a signed integer or floating-point "
           "expression. Vectorizes cleanly. Unlike in C, abs of a signed "
           "integer returns an unsigned integer of the same bit width. This "
           "means that abs of the most negative integer doesn't overflow.");

    p::def("absd", &h::absd, p::args("a", "b"),
           "Return the absolute difference between two values. Vectorizes "
           "cleanly. Returns an unsigned value of the same bit width. There are "
           "various ways to write this yourself, but they contain numerous "
           "gotchas and don't always compile to good code, so use this instead.");

    p::def("select", &select0, p::args("condition", "true_value", "false_value"),
           "Returns an expression similar to the ternary operator in C, except "
           "that it always evaluates all arguments. If the first argument is "
           "true, then return the second, else return the third. Typically "
           "vectorizes cleanly, but benefits from SSE41 or newer on x86.");

    p::def("select", &select1, p::args("c1", "v1", "c2", "v2", "default_val"),
           "A multi-way variant of select similar to a switch statement in C, "
           "which can accept multiple conditions and values in pairs. Evaluates "
           "to the first value for which the condition is true. Returns the "
           "final value if all conditions are false.");

    p::def("select", &select2, p::args(
                                   "c1", "v1",
                                   "c2", "v2",
                                   "c3", "v3",
                                   "default_val"));

    p::def("select", &select3, p::args(
                                   "c1", "v1",
                                   "c2", "v2",
                                   "c3", "v3",
                                   "c4", "v4",
                                   "default_val"));

    p::def("select", &select4, p::args(
                                   "c1", "v1",
                                   "c2", "v2",
                                   "c3", "v3",
                                   "c4", "v4",
                                   "c5", "v5",
                                   "default_val"));

    p::def("select", &select5, p::args(
                                   "c1", "v1",
                                   "c2", "v2",
                                   "c3", "v3",
                                   "c4", "v4",
                                   "c5", "v5",
                                   "c6", "v6",
                                   "default_val"));

    p::def("select", &select6, p::args(
                                   "c1", "v1",
                                   "c2", "v2",
                                   "c3", "v3",
                                   "c4", "v4",
                                   "c5", "v5",
                                   "c6", "v6",
                                   "c7", "v7",
                                   "default_val"));

    /*
    // Too many arguments for boost.python. Hopefully rare enough use case.
    p::def("select", &select7,
               (p::arg("c1"), "v1",
               "c2", "v2",
               "c3", "v3",
               "c4", "v4",
               "c5", "v5",
               "c6", "v6",
               "c7", "v7",
               "c8", "v8",
               "default_val"));

    p::def("select", &select8, (p::arg(
               "c1"), "v1",
               "c2", "v2",
               "c3", "v3",
               "c4", "v4",
               "c5", "v5",
               "c6", "v6",
               "c7", "v7",
               "c8", "v8",
               "c9", "v9",
               "default_val"));

    p::def("select", &select9, (p::arg(
               "c1"), "v1",
               "c2", "v2",
               "c3", "v3",
               "c4", "v4",
               "c5", "v5",
               "c6", "v6",
               "c7", "v7",
               "c8", "v8",
               "c9", "v9",
               "c10", "v10",
               "default_val"));*/

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

    p::def("sqrt", &h::sqrt, p::args("x"),
           "Return the square root of a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "Typically vectorizes cleanly.");

    p::def("hypot", &h::hypot, p::args("x"),
           "Return the square root of the sum of the squares of two "
           "floating-point expressions. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "Vectorizes cleanly.");

    p::def("exp", &h::exp, p::args("x"),
           "Return the exponential of a floating-point expression. If the "
           "argument is not floating-point, it is cast to Float(32). For "
           "Float(64) arguments, this calls the system exp function, and does "
           "not vectorize well. For Float(32) arguments, this function is "
           "vectorizable, does the right thing for extremely small or extremely "
           "large inputs, and is accurate up to the last bit of the "
           "mantissa. Vectorizes cleanly.");

    p::def("log", &h::log, p::args("x"),
           "Return the logarithm of a floating-point expression. If the "
           "argument is not floating-point, it is cast to Float(32). For "
           "Float(64) arguments, this calls the system log function, and does "
           "not vectorize well. For Float(32) arguments, this function is "
           "vectorizable, does the right thing for inputs <= 0 (returns -inf or "
           "nan), and is accurate up to the last bit of the "
           "mantissa. Vectorizes cleanly.");

    p::def("pow", &h::pow, p::args("x"),
           "Return one floating point expression raised to the power of "
           "another. The type of the result is given by the type of the first "
           "argument. If the first argument is not a floating-point type, it is "
           "cast to Float(32). For Float(32), cleanly vectorizable, and "
           "accurate up to the last few bits of the mantissa. Gets worse when "
           "approaching overflow. Vectorizes cleanly.");

    p::def("erf", &h::erf, p::args("x"),
           "Evaluate the error function erf. Only available for "
           "Float(32). Accurate up to the last three bits of the "
           "mantissa. Vectorizes cleanly.");

    p::def("fast_log", &h::fast_log, p::args("x"),
           "Fast approximate cleanly vectorizable log for Float(32). Returns "
           "nonsense for x <= 0.0f. Accurate up to the last 5 bits of the "
           "mantissa. Vectorizes cleanly.");

    p::def("fast_exp", &h::fast_exp, p::args("x"),
           "Fast approximate cleanly vectorizable exp for Float(32). Returns "
           "nonsense for inputs that would overflow or underflow. Typically "
           "accurate up to the last 5 bits of the mantissa. Gets worse when "
           "approaching overflow. Vectorizes cleanly.");

    p::def("fast_pow", &h::fast_pow, p::args("x"),
           "Fast approximate cleanly vectorizable pow for Float(32). Returns "
           "nonsense for x < 0.0f. Accurate up to the last 5 bits of the "
           "mantissa for typical exponents. Gets worse when approaching "
           "overflow. Vectorizes cleanly.");

    p::def("fast_inverse", &h::fast_inverse, p::args("x"),
           "Fast approximate inverse for Float(32). Corresponds to the rcpps "
           "instruction on x86, and the vrecpe instruction on ARM. "
           "Vectorizes cleanly.");

    p::def("fast_inverse_sqrt", &h::fast_inverse_sqrt, p::args("x"),
           "Fast approximate inverse square root for Float(32). Corresponds to "
           "the rsqrtps instruction on x86, and the vrsqrte instruction on "
           "ARM. Vectorizes cleanly.");

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
           "The return value is in floating point, even when it is a whole number. "
           "Vectorizes cleanly");

    p::def("is_nan", &h::is_nan, p::args("x"),
           "Returns true if the argument is a Not a Number (NaN). "
           "Requires a floating point argument.  Vectorizes cleanly.");

    p::def("reinterpret", &reinterpret0, p::args("t, e"),
           "Reinterpret the bits of one value as another type.");

    p::def("cast", &cast0, p::args("t", "e"),
           "Cast an expression to a new type.");

    p::def("print_when", &print_when0, (p::arg("condition"), p::arg("values")),
           "Create an Expr that prints whenever it is evaluated, provided that the condition is true.");

    p::def("lerp", &h::lerp, p::args("zero_val", "one_val", "weight"),
           "Linear interpolate between the two values according to a weight.\n"
           "\\param zero_val The result when weight is 0\n"
           "\\param one_val The result when weight is 1\n"
           "\\param weight The interpolation amount\n\n"

           "Both zero_val and one_val must have the same type. All types are "
           "supported, including bool.\n"

           "The weight is treated as its own type and must be float or an "
           "unsigned integer type. It is scaled to the bit-size of the type of "
           "x and y if they are integer, or converted to float if they are "
           "float. Integer weights are converted to float via division by the "
           "full-range value of the weight's type. Floating-point weights used "
           "to interpolate between integer values must be between 0.0f and "
           "1.0f, and an error may be signaled if it is not provably so. (clamp "
           "operators can be added to provide proof. Currently an error is only "
           "signalled for constant weights.)\n"

           "For integer linear interpolation, out of range values cannot be "
           "represented. In particular, weights that are conceptually less than "
           "0 or greater than 1.0 are not representable. As such the result is "
           "always between x and y (inclusive of course). For lerp with "
           "floating-point values and floating-point weight, the full range of "
           "a float is valid, however underflow and overflow can still occur.\n"

           "Ordering is not required between zero_val and one_val:\n"
           "    lerp(42, 69, .5f) == lerp(69, 42, .5f) == 56\n\n"

           "Results for integer types are for exactly rounded arithmetic. As "
           "such, there are cases where 16-bit and float differ because 32-bit "
           "floating-point (float) does not have enough precision to produce "
           "the exact result. (Likely true for 32-bit integer "
           "vs. double-precision floating-point as well.)\n"

           "At present, double precision and 64-bit integers are not supported.\n"

           "Generally, lerp will vectorize as if it were an operation on a type "
           "twice the bit size of the inferred type for x and y. ");

    p::def("popcount", &h::popcount, p::args("x"),
           "Count the number of set bits in an expression.");

    p::def("count_leading_zeros", &h::count_leading_zeros, p::args("x"),
           "Count the number of leading zero bits in an expression. The result is "
           "undefined if the value of the expression is zero.");

    p::def("count_trailing_zeros", &h::count_trailing_zeros, p::args("x"),
           "Count the number of trailing zero bits in an expression. The result is "
           "undefined if the value of the expression is zero.");

    p::def("random_float", &random_float1, p::args("seed"),
           "Return a random variable representing a uniformly distributed "
           "float in the half-open interval [0.0f, 1.0f). For random numbers of "
           "other types, use lerp with a random float as the last parameter.\n"

           "Optionally takes a seed.\n"

           "Note that:\n"
           "\\code\n"
           "Expr x = random_float();\n"
           "Expr y = x + x;\n"
           "\\endcode\n\n"

           "is very different to\n"
           "\\code\n"
           "Expr y = random_float() + random_float();\n"
           "\\endcode\n\n"

           "The first doubles a random variable, and the second adds two "
           "independent random variables.\n"

           "A given random variable takes on a unique value that depends "
           "deterministically on the pure variables of the function they belong "
           "to, the identity of the function itself, and which definition of "
           "the function it is used in. They are, however, shared across tuple "
           "elements.\n"

           "This function vectorizes cleanly.");
    p::def("random_float", &random_float0);  // no args

    p::def("random_int", &random_int1, p::args("seed"),
           "Return a random variable representing a uniformly distributed "
           "32-bit integer. See \\ref random_float. Vectorizes cleanly.");
    p::def("random_int", &random_int0);  // no args

    p::def("undef", &undef0, p::args("type"),
           "Return an undef value of the given type. Halide skips stores that "
           "depend on undef values, so you can use this to mean \"do not modify "
           "this memory location\". This is an escape hatch that can be used for "
           "several things:\n"

           "You can define a reduction with no pure step, by setting the pure "
           "step to undef. Do this only if you're confident that the update "
           "steps are sufficient to correctly fill in the domain.\n"

           "For a tuple-valued reduction, you can write an update step that "
           "only updates some tuple elements.\n"

           "You can define single-stage pipeline that only has update steps,"
           "and depends on the values already in the output buffer.\n"

           "Use this feature with great caution, as you can use it to load from "
           "uninitialized memory.\n");

    p::def("memoize_tag", &memoize_tag0, p::args("result", "cache_key_values"),
           "Control the values used in the memoization cache key for memoize. "
           "Normally parameters and other external dependencies are "
           "automatically inferred and added to the cache key. The memoize_tag "
           "operator allows computing one expression and using either the "
           "computed value, or one or more other expressions in the cache key "
           "instead of the parameter dependencies of the computation. The "
           "single argument version is completely safe in that the cache key "
           "will use the actual computed value -- it is difficult or imposible "
           "to produce erroneous caching this way. The more-than-one argument "
           "version allows generating cache keys that do not uniquely identify "
           "the computation and thus can result in caching errors.n"

           "A potential use for the single argument version is to handle a "
           "floating-point parameter that is quantized to a small "
           "integer. Mutliple values of the float will produce the same integer "
           "and moving the caching to using the integer for the key is more "
           "efficient.\n"

           "The main use for the more-than-one argument version is to provide "
           "cache key information for Handles and ImageParams, which otherwise "
           "are not allowed inside compute_cached operations. E.g. when passing "
           "a group of parameters to an external array function via a Handle, "
           "memoize_tag can be used to isolate the actual values used by that "
           "computation. If an ImageParam is a constant image with a persistent "
           "digest, memoize_tag can be used to key computations using that image "
           "on the digest.");
    //Expr memoize_tag(Expr result, const std::vector<Expr> &cache_key_values);

    //template<typename ...Args>
    //Expr memoize_tag(Expr result, Args... args)

    p::def("likely", &h::likely, p::args("e"),
           "Expressions tagged with this intrinsic are considered to be part "
           "of the steady state of some loop with a nasty beginning and end "
           "(e.g. a boundary condition). When Halide encounters likely "
           "intrinsics, it splits the containing loop body into three, and "
           "tries to simplify down all conditions that lead to the likely. For "
           "example, given the expression: select(x < 1, bar, x > 10, bar, "
           "likely(foo)), Halide will split the loop over x into portions where "
           "x < 1, 1 <= x <= 10, and x > 10.\n"

           "You're unlikely to want to call this directly. You probably want to "
           "use the boundary condition helpers in the BoundaryConditions "
           "namespace instead. ");

    return;
}

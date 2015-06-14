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

h::Expr select0(h::Expr condition, h::Expr true_value, h::Expr false_value)
{
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

h::Expr print_when0(h::Expr condition, p::tuple values_passed)
{
    const size_t num_values = p::len(values_passed);
    std::vector<h::Expr> values;
    values.reserve(num_values);

    for(size_t i=0; i < num_values; i += 1)
    {
        p::object o = values_passed[i];
        p::extract<h::Expr &> expr_extract(o);

        if(expr_extract.check())
        {
            values.push_back(expr_extract());
        }
        else
        {
            for(size_t j=0; j < num_values; j+=1)
            {
                p::object o = values_passed[j];
                const std::string o_str = p::extract<std::string>(p::str(o));
                printf("print_when values[%lu] == %s\n", j, o_str.c_str());
            }
            throw std::invalid_argument("print_when only handles a list/tuple of (convertible to) Expr.");
        }
    }

    return h::print_when(condition, values);
}


void defineOperators()
{
    // defined in IROperator.h

    p::def("max", &h::max,
           p::args("a", "b"),
           "Returns an expression representing the greater of the two "
           "arguments, after doing any necessary type coercion using "
           "\\ref Internal::match_types. Vectorizes cleanly on most platforms "
           "(with the exception of integer types on x86 without SSE4).");

    p::def("min", &h::min,
           p::args("a", "b"),
           "Returns an expression representing the greater of the two "
           "arguments, after doing any necessary type coercion using "
           "\\ref Internal::match_types. Vectorizes cleanly on most platforms "
           "(with the exception of integer types on x86 without SSE4).");

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

    return;
}

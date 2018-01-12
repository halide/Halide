#include "PyIROperator.h"

namespace Halide {
namespace PythonBindings {

namespace {

Expr reinterpret0(Type t, Expr e) {
    return reinterpret(t, e);
}

Expr cast0(Type t, Expr e) {
    return Halide::cast(t, e);
}

Expr select0(Expr condition, Expr true_value, Expr false_value) {
    return select(condition, true_value, false_value);
}

Expr select1(Expr c1, Expr v1,
                Expr c2, Expr v2,
                Expr default_val) {
    return select(c1, v1,
                     c2, v2, default_val);
}
Expr select2(Expr c1, Expr v1,
                Expr c2, Expr v2,
                Expr c3, Expr v3,
                Expr default_val) {
    return select(c1, v1,
                     c2, v2,
                     c3, v3, default_val);
}
Expr select3(Expr c1, Expr v1,
                Expr c2, Expr v2,
                Expr c3, Expr v3,
                Expr c4, Expr v4,
                Expr default_val) {
    return select(c1, v1,
                     c2, v2,
                     c3, v3,
                     c4, v4, default_val);
}
Expr select4(Expr c1, Expr v1,
                Expr c2, Expr v2,
                Expr c3, Expr v3,
                Expr c4, Expr v4,
                Expr c5, Expr v5,
                Expr default_val) {
    return select(c1, v1,
                     c2, v2,
                     c3, v3,
                     c4, v4,
                     c5, v5, default_val);
}
Expr select5(Expr c1, Expr v1,
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
                     c6, v6, default_val);
}
Expr select6(Expr c1, Expr v1,
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
                     c7, v7, default_val);
}

std::vector<Expr> tuple_to_exprs(py::tuple t) {
    const size_t c = py::len(t);
    std::vector<Expr> exprs;
    exprs.reserve(c);

    for (size_t i = 0; i < c; i += 1) {
        py::object o = t[i];
        Expr e;
        py::extract<Expr> expr_extract(o);
        if (expr_extract.check()) {
            e = expr_extract();
        } else {
            // Python 'str' is not implicitly convertible to Expr,
            // but in this context we want to explicitly check and convert.
            py::extract<std::string> string_extract(o);
            if (string_extract.check()) {
                e = Expr(string_extract());
            } else {
                const std::string o_str = py::extract<std::string>(py::str(o));
                throw std::invalid_argument("The value '" + o_str + "' is not convertible to Expr.");
            }
        }
        exprs.push_back(e);
    }
    return exprs;
}

py::object print(py::tuple args, py::dict kwargs) {
    return py::object(print(tuple_to_exprs(args)));
}

py::object print_when(py::tuple args, py::dict kwargs) {
    Expr condition = py::extract<Expr>(args[0]);
    return py::object(print_when(condition, tuple_to_exprs(py::extract<py::tuple>(args.slice(1, py::_)))));
}

Expr random_float0() {
    return random_float();
}

Expr random_float1(Expr seed) {
    return random_float(seed);
}

Expr random_int0() {
    return random_int();
}

Expr random_int1(Expr seed) {
    return random_int(seed);
}

Expr undef0(Type type) {
    return undef(type);
}

Expr memoize_tag0(Expr result, const std::vector<Expr> &cache_key_values) {
    return memoize_tag(result, cache_key_values);
}

// py::def() doesn't allow specifying a docstring with raw_function();
// this is some simple sugar to allow for it.
// TODO: unfortunately, using raw_function() means that we don't get
// any free type info about the Python prototype look; we should
// probably augment docstrings that use this appropriately.
template <class F>
py::object def_raw(const char *name, F f, size_t min_args, const char *docstring) {
    py::object o = py::raw_function(f, min_args);
    py::def(name, o);
    // Must call setattr *after* def
    py::setattr(o, "__doc__", py::str(docstring));
    return o;
}

}  // namespace

void define_operators() {
    // defined in IROperator.h

    Expr (*max_exprs)(Expr, Expr) = &max;
    Expr (*max_expr_int)(Expr, int) = &max;
    Expr (*max_int_expr)(int, Expr) = &max;

    Expr (*min_exprs)(Expr, Expr) = &min;
    Expr (*min_expr_int)(Expr, int) = &min;
    Expr (*min_int_expr)(int, Expr) = &min;

    py::def("max", max_exprs,
           py::args("a", "b"),
           "Returns an expression representing the greater of the two "
           "arguments, after doing any necessary type coercion using "
           "Internal::match_types. Vectorizes cleanly on most platforms "
           "(with the exception of integer types on x86 without SSE4).");

    py::def("max", max_expr_int,
           py::args("a", "b"),
           "Returns an expression representing the greater of an expression"
           " and a constant integer.  The integer is coerced to the type of the"
           " expression. Errors if the integer is not representable as that"
           " type. Vectorizes cleanly on most platforms (with the exception of"
           " integer types on x86 without SSE4).");

    py::def("max", max_int_expr,
           py::args("a", "b"),
           "Returns an expression representing the greater of a constant"
           " integer and an expression. The integer is coerced to the type of"
           " the expression. Errors if the integer is not representable as that"
           " type. Vectorizes cleanly on most platforms (with the exception of"
           " integer types on x86 without SSE4).");

    py::def("min", min_exprs,
           py::args("a", "b"),
           "Returns an expression representing the greater of the two "
           "arguments, after doing any necessary type coercion using "
           "Internal::match_types. Vectorizes cleanly on most platforms "
           "(with the exception of integer types on x86 without SSE4).");

    py::def("min", min_expr_int,
           py::args("a", "b"),
           "Returns an expression representing the lesser of an expression"
           " and a constant integer.  The integer is coerced to the type of the"
           " expression. Errors if the integer is not representable as that"
           " type. Vectorizes cleanly on most platforms (with the exception of"
           " integer types on x86 without SSE4).");

    py::def("min", min_int_expr,
           py::args("a", "b"),
           "Returns an expression representing the lesser of a constant"
           " integer and an expression. The integer is coerced to the type of"
           " the expression. Errors if the integer is not representable as that"
           " type. Vectorizes cleanly on most platforms (with the exception of"
           " integer types on x86 without SSE4).");

    py::def("clamp", &clamp,
           py::args("a", "min_val", "max_val"),
           "Clamps an expression to lie within the given bounds. The bounds "
           "are type-cast to match the expression. Vectorizes as well as min/max.");

    py::def("abs", &abs, py::args("a"),
           "Returns the absolute value of a signed integer or floating-point "
           "expression. Vectorizes cleanly. Unlike in C, abs of a signed "
           "integer returns an unsigned integer of the same bit width. This "
           "means that abs of the most negative integer doesn't overflow.");

    py::def("absd", &absd, py::args("a", "b"),
           "Return the absolute difference between two values. Vectorizes "
           "cleanly. Returns an unsigned value of the same bit width. There are "
           "various ways to write this yourself, but they contain numerous "
           "gotchas and don't always compile to good code, so use this instead.");

    py::def("select", &select0, py::args("condition", "true_value", "false_value"),
           "Returns an expression similar to the ternary operator in C, except "
           "that it always evaluates all arguments. If the first argument is "
           "true, then return the second, else return the third. Typically "
           "vectorizes cleanly, but benefits from SSE41 or newer on x86.");

    py::def("select", &select1, py::args("c1", "v1", "c2", "v2", "default_val"),
           "A multi-way variant of select similar to a switch statement in C, "
           "which can accept multiple conditions and values in pairs. Evaluates "
           "to the first value for which the condition is true. Returns the "
           "final value if all conditions are false.");

    py::def("select", &select2, py::args(
                                   "c1", "v1",
                                   "c2", "v2",
                                   "c3", "v3",
                                   "default_val"));

    py::def("select", &select3, py::args(
                                   "c1", "v1",
                                   "c2", "v2",
                                   "c3", "v3",
                                   "c4", "v4",
                                   "default_val"));

    py::def("select", &select4, py::args(
                                   "c1", "v1",
                                   "c2", "v2",
                                   "c3", "v3",
                                   "c4", "v4",
                                   "c5", "v5",
                                   "default_val"));

    py::def("select", &select5, py::args(
                                   "c1", "v1",
                                   "c2", "v2",
                                   "c3", "v3",
                                   "c4", "v4",
                                   "c5", "v5",
                                   "c6", "v6",
                                   "default_val"));

    py::def("select", &select6, py::args(
                                   "c1", "v1",
                                   "c2", "v2",
                                   "c3", "v3",
                                   "c4", "v4",
                                   "c5", "v5",
                                   "c6", "v6",
                                   "c7", "v7",
                                   "default_val"));

    // sin, cos, tan @{
    py::def("sin", &sin, py::args("x"),
           "Return the sine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    py::def("asin", &asin, py::args("x"),
           "Return the arcsine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    py::def("cos", &cos, py::args("x"),
           "Return the cosine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    py::def("acos", &acos, py::args("x"),
           "Return the arccosine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    py::def("tan", &tan, py::args("x"),
           "Return the tangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    py::def("atan", &atan, py::args("x"),
           "Return the arctangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    py::def("atan", &atan2, py::args("x", "y"),
           "Return the arctangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    py::def("atan2", &atan2, py::args("x", "y"),
           "Return the arctangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");
    // @}

    // sinh, cosh, tanh @{
    py::def("sinh", &sinh, py::args("x"),
           "Return the hyperbolic sine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    py::def("asinh", &asinh, py::args("x"),
           "Return the hyperbolic arcsine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    py::def("cosh", &cosh, py::args("x"),
           "Return the hyperbolic cosine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    py::def("acosh", &acosh, py::args("x"),
           "Return the hyperbolic arccosine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    py::def("tanh", &tanh, py::args("x"),
           "Return the hyperbolic tangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    py::def("atanh", &atanh, py::args("x"),
           "Return the hyperbolic arctangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");
    // @}

    py::def("sqrt", &sqrt, py::args("x"),
           "Return the square root of a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "Typically vectorizes cleanly.");

    py::def("hypot", &hypot, py::args("x"),
           "Return the square root of the sum of the squares of two "
           "floating-point expressions. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "Vectorizes cleanly.");

    py::def("exp", &exp, py::args("x"),
           "Return the exponential of a floating-point expression. If the "
           "argument is not floating-point, it is cast to Float(32). For "
           "Float(64) arguments, this calls the system exp function, and does "
           "not vectorize well. For Float(32) arguments, this function is "
           "vectorizable, does the right thing for extremely small or extremely "
           "large inputs, and is accurate up to the last bit of the "
           "mantissa. Vectorizes cleanly.");

    py::def("log", &log, py::args("x"),
           "Return the logarithm of a floating-point expression. If the "
           "argument is not floating-point, it is cast to Float(32). For "
           "Float(64) arguments, this calls the system log function, and does "
           "not vectorize well. For Float(32) arguments, this function is "
           "vectorizable, does the right thing for inputs <= 0 (returns -inf or "
           "nan), and is accurate up to the last bit of the "
           "mantissa. Vectorizes cleanly.");

    py::def("pow", &pow, py::args("x"),
           "Return one floating point expression raised to the power of "
           "another. The type of the result is given by the type of the first "
           "argument. If the first argument is not a floating-point type, it is "
           "cast to Float(32). For Float(32), cleanly vectorizable, and "
           "accurate up to the last few bits of the mantissa. Gets worse when "
           "approaching overflow. Vectorizes cleanly.");

    py::def("erf", &erf, py::args("x"),
           "Evaluate the error function erf. Only available for "
           "Float(32). Accurate up to the last three bits of the "
           "mantissa. Vectorizes cleanly.");

    py::def("fast_log", &fast_log, py::args("x"),
           "Fast approximate cleanly vectorizable log for Float(32). Returns "
           "nonsense for x <= 0.0f. Accurate up to the last 5 bits of the "
           "mantissa. Vectorizes cleanly.");

    py::def("fast_exp", &fast_exp, py::args("x"),
           "Fast approximate cleanly vectorizable exp for Float(32). Returns "
           "nonsense for inputs that would overflow or underflow. Typically "
           "accurate up to the last 5 bits of the mantissa. Gets worse when "
           "approaching overflow. Vectorizes cleanly.");

    py::def("fast_pow", &fast_pow, py::args("x"),
           "Fast approximate cleanly vectorizable pow for Float(32). Returns "
           "nonsense for x < 0.0f. Accurate up to the last 5 bits of the "
           "mantissa for typical exponents. Gets worse when approaching "
           "overflow. Vectorizes cleanly.");

    py::def("fast_inverse", &fast_inverse, py::args("x"),
           "Fast approximate inverse for Float(32). Corresponds to the rcpps "
           "instruction on x86, and the vrecpe instruction on ARM. "
           "Vectorizes cleanly.");

    py::def("fast_inverse_sqrt", &fast_inverse_sqrt, py::args("x"),
           "Fast approximate inverse square root for Float(32). Corresponds to "
           "the rsqrtps instruction on x86, and the vrsqrte instruction on "
           "ARM. Vectorizes cleanly.");

    py::def("floor", &floor, py::args("x"),
           "Return the greatest whole number less than or equal to a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    py::def("ceil", &ceil, py::args("x"),
           "Return the least whole number less than or equal to a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    py::def("round", &round, py::args("x"),
           "Return the whole number closest to a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    py::def("trunc", &trunc, py::args("x"),
           "Return the integer part of a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    py::def("fract", &fract, py::args("x"),
           "Return the fractional part of a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is in floating point, even when it is a whole number. "
           "Vectorizes cleanly");

    py::def("is_nan", &is_nan, py::args("x"),
           "Returns true if the argument is a Not a Number (NaN). "
           "Requires a floating point argument.  Vectorizes cleanly.");

    py::def("reinterpret", &reinterpret0, py::args("t, e"),
           "Reinterpret the bits of one value as another type.");

    py::def("cast", &cast0, py::args("t", "e"),
           "Cast an expression to a new type.");

    def_raw("print_when", print_when, 2,
           "Create an Expr that prints whenever it is evaluated, "
           "provided that the condition is true.");

    def_raw("print", print, 1,
           "Create an Expr that prints out its value whenever it is "
           "evaluated. It also prints out everything else in the arguments "
           "list, separated by spaces. This can include string literals.");

    py::def("lerp", &lerp, py::args("zero_val", "one_val", "weight"),
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

    py::def("popcount", &popcount, py::args("x"),
           "Count the number of set bits in an expression.");

    py::def("count_leading_zeros", &count_leading_zeros, py::args("x"),
           "Count the number of leading zero bits in an expression. The result is "
           "undefined if the value of the expression is zero.");

    py::def("count_trailing_zeros", &count_trailing_zeros, py::args("x"),
           "Count the number of trailing zero bits in an expression. The result is "
           "undefined if the value of the expression is zero.");

    py::def("random_float", &random_float1, py::args("seed"),
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
    py::def("random_float", &random_float0);  // no args

    py::def("random_int", &random_int1, py::args("seed"),
           "Return a random variable representing a uniformly distributed "
           "32-bit integer. See \\ref random_float. Vectorizes cleanly.");
    py::def("random_int", &random_int0);  // no args

    py::def("undef", &undef0, py::args("type"),
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

    py::def("memoize_tag", &memoize_tag0, py::args("result", "cache_key_values"),
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

    py::def("likely", &likely, py::args("e"),
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
}

}  // namespace PythonBindings
}  // namespace Halide

#include "PyIROperator.h"

namespace Halide {
namespace PythonBindings {

namespace {

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

// TODO: clever template usage could generalize this to list-of-types-to-try.
std::vector<Expr> args_to_vector_for_print(const py::args &args, size_t start_offset = 0) {
    if (args.size() < start_offset) {
        throw py::value_error("Not enough arguments");
    }
    std::vector<Expr> v;
    v.reserve(args.size() - (start_offset));
    for (size_t i = start_offset; i < args.size(); ++i) {
        // No way to see if a cast will work: just have to try
        // and fail. Normally we don't want string to be convertible
        // to Expr, but in this unusual case we do.
        try {
            v.push_back(args[i].cast<std::string>());
        } catch (...) {
            v.push_back(args[i].cast<Expr>());
        }
    }
    return v;
}

}  // namespace

void define_operators(py::module &m) {
    m.def("max", (Expr (*)(Expr, Expr)) &max);
    m.def("max", (Expr (*)(Expr, int)) &max);
    m.def("max", (Expr (*)(int, Expr)) &max);
    m.def("min", (Expr (*)(Expr, Expr)) &min);
    m.def("min", (Expr (*)(Expr, int)) &min);
    m.def("min", (Expr (*)(int, Expr)) &min);
    m.def("clamp", &clamp);
    m.def("abs", &abs);
    m.def("absd", &absd);

    // TODO: improve this to use a single overload and py::args
    m.def("select", &select0);
    m.def("select", &select1);
    m.def("select", &select2);
    m.def("select", &select3);
    m.def("select", &select4);
    m.def("select", &select5);
    m.def("select", &select6);

    m.def("sin", &sin);
    m.def("asin", &asin);
    m.def("cos", &cos);
    m.def("acos", &acos);
    m.def("tan", &tan);
    m.def("atan", &atan);
    m.def("atan", &atan2);
    m.def("atan2", &atan2);
    m.def("sinh", &sinh);
    m.def("asinh", &asinh);
    m.def("cosh", &cosh);
    m.def("acosh", &acosh);
    m.def("tanh", &tanh);
    m.def("atanh", &atanh);
    m.def("sqrt", &sqrt);
    m.def("hypot", &hypot);
    m.def("exp", &exp);
    m.def("log", &log);
    m.def("pow", &pow);
    m.def("erf", &erf);
    m.def("fast_log", &fast_log);
    m.def("fast_exp", &fast_exp);
    m.def("fast_pow", &fast_pow);
    m.def("fast_inverse", &fast_inverse);
    m.def("fast_inverse_sqrt", &fast_inverse_sqrt);
    m.def("floor", &floor);
    m.def("ceil", &ceil);
    m.def("round", &round);
    m.def("trunc", &trunc);
    m.def("fract", &fract);
    m.def("is_nan", &is_nan);
    m.def("reinterpret", (Expr (*)(Type, Expr)) &reinterpret);
    m.def("cast", (Expr (*)(Type, Expr)) &cast);

    // variadic args need a little extra love.
    m.def("print", [](py::args args) -> Expr {
        return print(args_to_vector_for_print(args));
    });

    // variadic args need a little extra love.
    m.def("print_when", [](py::args args) -> Expr {
        Expr cond = args[0].cast<Expr>();
        return print_when(cond, args_to_vector_for_print(args, 1));
    });

    m.def("lerp", &lerp);
    m.def("popcount", &popcount);
    m.def("count_leading_zeros", &count_leading_zeros);
    m.def("count_trailing_zeros", &count_trailing_zeros);
    m.def("random_float", (Expr (*)(Expr)) &random_float);
    m.def("random_float", (Expr (*)()) &random_float);
    m.def("random_int", (Expr (*)(Expr)) &random_int);
    m.def("random_int", (Expr (*)()) &random_int);
    m.def("undef", (Expr (*)(Type)) &undef);

    m.def("likely", &likely);
}

}  // namespace PythonBindings
}  // namespace Halide

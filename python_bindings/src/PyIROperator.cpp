#include "PyIROperator.h"

#include "PyTuple.h"

namespace Halide {
namespace PythonBindings {

namespace {

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
    m.def("max", [](py::args args) -> Expr {
        if (args.size() < 2) {
            throw py::value_error("max() must have at least 2 arguments");
        }
        int pos = (int) args.size() - 1;
        Expr value = args[pos--].cast<Expr>();
        while (pos >= 0) {
            value = max(args[pos--].cast<Expr>(), value);
        }
        return value;
    });

    m.def("min", [](py::args args) -> Expr {
        if (args.size() < 2) {
            throw py::value_error("min() must have at least 2 arguments");
        }
        int pos = (int) args.size() - 1;
        Expr value = args[pos--].cast<Expr>();
        while (pos >= 0) {
            value = min(args[pos--].cast<Expr>(), value);
        }
        return value;
    });

    m.def("clamp", &clamp);
    m.def("abs", &abs);
    m.def("absd", &absd);

    m.def("select", [](py::args args) -> Expr {
        if (args.size() < 3) {
            throw py::value_error("select() must have at least 3 arguments");
        }
        if ((args.size() % 2) == 0) {
            throw py::value_error("select() must have an odd number of arguments");
        }
        int pos = (int) args.size() - 1;
        Expr false_value = args[pos--].cast<Expr>();
        while (pos > 0) {
            Expr true_value = args[pos--].cast<Expr>();
            Expr condition = args[pos--].cast<Expr>();
            false_value = select(condition, true_value, false_value);
        }
        return false_value;
    });

    m.def("tuple_select", [](py::args args) -> py::tuple {
        _halide_user_assert(args.size() >= 3)
            << "tuple_select() must have at least 3 arguments";
        _halide_user_assert((args.size() % 2) != 0)
            << "tuple_select() must have an odd number of arguments";

        int pos = (int) args.size() - 1;
        Tuple false_value = args[pos--].cast<Tuple>();
        bool has_tuple_cond = false, has_expr_cond = false;
        while (pos > 0) {
            Tuple true_value = args[pos--].cast<Tuple>();;
            // Note that 'condition' can be either Expr or Tuple, but must be consistent across all
            py::object py_cond = args[pos--];
            Expr expr_cond;
            Tuple tuple_cond(expr_cond);
            try {
                tuple_cond = py_cond.cast<Tuple>();
                has_tuple_cond = true;
            } catch (...) {
                expr_cond = py_cond.cast<Expr>();
                has_expr_cond = true;
            }

            if (expr_cond.defined()) {
                false_value = tuple_select(expr_cond, true_value, false_value);
            } else {
                false_value = tuple_select(tuple_cond, true_value, false_value);
            }
        }
        _halide_user_assert(!(has_tuple_cond && has_expr_cond))
            <<"tuple_select() may not mix Expr and Tuple for the condition elements.";
        return to_python_tuple(false_value);
    });

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
    m.def("is_inf", &is_inf);
    m.def("is_finite", &is_finite);
    m.def("reinterpret", (Expr (*)(Type, Expr)) &reinterpret);
    m.def("cast", (Expr (*)(Type, Expr)) &cast);
    m.def("print", [](py::args args) -> Expr {
        return print(args_to_vector_for_print(args));
    });
    m.def("print_when", [](Expr condition, py::args args) -> Expr {
        return print_when(condition, args_to_vector_for_print(args));
    }, py::arg("condition"));
    m.def("require", [](Expr condition, Expr value, py::args args) -> Expr {
        auto v = args_to_vector<Expr>(args);
        v.insert(v.begin(), value);
        return require(condition, v);
    }, py::arg("condition"), py::arg("value"));
    m.def("lerp", &lerp);
    m.def("popcount", &popcount);
    m.def("count_leading_zeros", &count_leading_zeros);
    m.def("count_trailing_zeros", &count_trailing_zeros);
    m.def("div_round_to_zero", &div_round_to_zero);
    m.def("mod_round_to_zero", &mod_round_to_zero);
    m.def("random_float", (Expr (*)()) &random_float);
    m.def("random_uint", (Expr (*)()) &random_uint);
    m.def("random_int", (Expr (*)()) &random_int);
    m.def("random_float", (Expr (*)(Expr)) &random_float, py::arg("seed"));
    m.def("random_uint", (Expr (*)(Expr)) &random_uint, py::arg("seed"));
    m.def("random_int", (Expr (*)(Expr)) &random_int, py::arg("seed"));
    m.def("undef", (Expr (*)(Type)) &undef);
    m.def("memoize_tag", [](Expr result, py::args cache_key_values) -> Expr {
        return Internal::memoize_tag_helper(result, args_to_vector<Expr>(cache_key_values));
    }, py::arg("result"));
    m.def("likely", &likely);
    m.def("likely_if_innermost", &likely_if_innermost);
    m.def("saturating_cast", (Expr (*)(Type, Expr))&saturating_cast);
    m.def("strict_float", &strict_float);
}

}  // namespace PythonBindings
}  // namespace Halide

#include "PyIROperator.h"

#include <utility>

#include "PyTuple.h"

namespace Halide {

// Only necessary for debugging
// std::ostream &operator<<(std::ostream &stream, const Halide::Tuple &t) {
//     stream << "Tup{";
//     for (size_t i = 0; i < t.size(); i++) {
//         stream << t[i] << ",";
//     }
//     stream << "}";
//     return stream;
// }

namespace PythonBindings {

void define_operators(py::module &m) {
    m.def("max", [](const py::args &args) -> Expr {
        if (args.size() < 2) {
            throw py::value_error("max() must have at least 2 arguments");
        }
        int pos = (int)args.size() - 1;
        Expr value = args[pos--].cast<Expr>();
        while (pos >= 0) {
            value = max(args[pos--].cast<Expr>(), value);
        }
        return value;
    });

    m.def("min", [](const py::args &args) -> Expr {
        if (args.size() < 2) {
            throw py::value_error("min() must have at least 2 arguments");
        }
        int pos = (int)args.size() - 1;
        Expr value = args[pos--].cast<Expr>();
        while (pos >= 0) {
            value = min(args[pos--].cast<Expr>(), value);
        }
        return value;
    });

    m.def("clamp", &clamp);
    m.def("unsafe_promise_clamped", &unsafe_promise_clamped);
    m.def("abs", &abs);
    m.def("absd", &absd);

    m.def("select", [](const py::args &args) -> py::object {
        if (args.size() < 3) {
            throw py::value_error("select() must have at least 3 arguments");
        }
        if ((args.size() % 2) == 0) {
            throw py::value_error("select() must have an odd number of arguments");
        }

        // Tricky set of options here:
        //
        // - (Expr, Expr, Expr, [Expr, Expr...]) -> Expr
        // - (Expr, Tuple, Tuple, [Tuple, Tuple...]) -> Tuple   [Tuples must be same arity]
        // - (Tuple, Tuple, Tuple, [Tuple, Tuple...]) -> Tuple  [Tuples must be same arity]
        //
        // It's made trickier by the fact that it's hard to do a reliable "is-a" check for Tuple here,
        // so we'll do the slow-but-reliable approach of just trying to cast to Tuple and catching
        // exceptions.

        std::string tuple_error_msg;
        try {
            int pos = (int)args.size() - 1;
            Tuple false_value = args[pos--].cast<Tuple>();
            bool has_tuple_cond = false;
            bool has_expr_cond = false;
            while (pos > 0) {
                Tuple true_value = args[pos--].cast<Tuple>();
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

                if (has_tuple_cond && has_expr_cond) {
                    // We don't want to throw an error here, since the catch(...) would catch it,
                    // and it would be hard to distinguish from other errors. Just set the string here
                    // and jump to the error reporter outside the catch.
                    tuple_error_msg = "tuple_select() may not mix Expr and Tuple for the condition elements.";
                    goto handle_tuple_error;
                }

                if (expr_cond.defined()) {
                    false_value = select(expr_cond, true_value, false_value);
                } else {
                    if (tuple_cond.size() != true_value.size() || true_value.size() != false_value.size()) {
                        // We don't want to throw an error here, since the catch(...) would catch it,
                        // and it would be hard to distinguish from other errors. Just set the string here
                        // and jump to the error reporter outside the catch.
                        tuple_error_msg = "select() on Tuples requires all Tuples to have identical sizes.";
                        goto handle_tuple_error;
                    }
                    false_value = select(tuple_cond, true_value, false_value);
                }
            }
            return to_python_tuple(false_value);

        } catch (...) {
            // fall thru and try the Expr case
        }

    handle_tuple_error:
        if (!tuple_error_msg.empty()) {
            _halide_user_assert(false) << tuple_error_msg;
        }

        int pos = (int)args.size() - 1;
        Expr false_expr_value = args[pos--].cast<Expr>();
        while (pos > 0) {
            Expr true_expr_value = args[pos--].cast<Expr>();
            Expr condition_expr = args[pos--].cast<Expr>();
            false_expr_value = select(condition_expr, true_expr_value, false_expr_value);
        }
        return py::cast(false_expr_value);
    });

    m.def("tuple_select", [](const py::args &args) -> py::tuple {
        // HALIDE_ATTRIBUTE_DEPRECATED("tuple_select has been deprecated. Use select instead (which now works for Tuples)")
        PyErr_WarnEx(PyExc_DeprecationWarning,
                     "tuple_select has been deprecated. Use select instead (which now works for Tuples)",
                     1);

        _halide_user_assert(args.size() >= 3)
            << "tuple_select() must have at least 3 arguments";
        _halide_user_assert((args.size() % 2) != 0)
            << "tuple_select() must have an odd number of arguments";

        int pos = (int)args.size() - 1;
        Tuple false_value = args[pos--].cast<Tuple>();
        bool has_tuple_cond = false, has_expr_cond = false;
        while (pos > 0) {
            Tuple true_value = args[pos--].cast<Tuple>();
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
                false_value = select(expr_cond, true_value, false_value);
            } else {
                false_value = select(tuple_cond, true_value, false_value);
            }
        }
        _halide_user_assert(!(has_tuple_cond && has_expr_cond))
            << "tuple_select() may not mix Expr and Tuple for the condition elements.";
        return to_python_tuple(false_value);
    });
    m.def("mux", (Expr(*)(const Expr &, const std::vector<Expr> &)) & mux);

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
    m.def("reinterpret", (Expr(*)(Type, Expr)) & reinterpret);
    m.def("cast", (Expr(*)(Type, Expr)) & cast);
    m.def("print", [](const py::args &args) -> Expr {
        return print(collect_print_args(args));
    });
    m.def(
        "print_when", [](const Expr &condition, const py::args &args) -> Expr {
            return print_when(condition, collect_print_args(args));
        },
        py::arg("condition"));
    m.def(
        "require", [](const Expr &condition, const Expr &value, const py::args &args) -> Expr {
            auto v = args_to_vector<Expr>(args);
            v.insert(v.begin(), value);
            return require(condition, v);
        },
        py::arg("condition"), py::arg("value"));
    m.def("lerp", &lerp);
    m.def("popcount", &popcount);
    m.def("count_leading_zeros", &count_leading_zeros);
    m.def("count_trailing_zeros", &count_trailing_zeros);
    m.def("div_round_to_zero", &div_round_to_zero);
    m.def("mod_round_to_zero", &mod_round_to_zero);
    m.def("random_float", (Expr(*)()) & random_float);
    m.def("random_uint", (Expr(*)()) & random_uint);
    m.def("random_int", (Expr(*)()) & random_int);
    m.def("random_float", (Expr(*)(Expr)) & random_float, py::arg("seed"));
    m.def("random_uint", (Expr(*)(Expr)) & random_uint, py::arg("seed"));
    m.def("random_int", (Expr(*)(Expr)) & random_int, py::arg("seed"));
    m.def("undef", (Expr(*)(Type)) & undef);
    m.def(
        "memoize_tag", [](const Expr &result, const py::args &cache_key_values) -> Expr {
            return Internal::memoize_tag_helper(result, args_to_vector<Expr>(cache_key_values));
        },
        py::arg("result"));
    m.def("likely", &likely);
    m.def("likely_if_innermost", &likely_if_innermost);
    m.def("saturating_cast", (Expr(*)(Type, Expr)) & saturating_cast);
    m.def("strict_float", &strict_float);
    m.def("logical_not", [](const Expr &expr) -> Expr {
        return !expr;
    });
}

}  // namespace PythonBindings
}  // namespace Halide

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

namespace {

bool is_expr(const py::handle &obj) {
    try {
        (void)obj.cast<Expr>();  // Check if casting succeeds
        return true;
    } catch (const py::cast_error &) {
        return false;
    }
}

template<typename T>
T cast_arg(const py::handle &arg) {
    try {
        return arg.cast<T>();
    } catch (const py::cast_error &) {
        _halide_user_error
            << "select(): Expected " << py::str(py::type::of<T>().attr("__name__"))
            << " but got " << py::str(arg.get_type().attr("__name__")) << ": "
            << py::str(arg);
    }
}

template<typename TCond, typename TVal>
py::object py_select_reduce(const py::args &args) {
    auto false_case = cast_arg<TVal>(args[args.size() - 1]);
    for (size_t pos = args.size() - 1; pos >= 2; pos -= 2) {
        auto true_case = cast_arg<TVal>(args[pos - 1]);
        auto condition = cast_arg<TCond>(args[pos - 2]);
        false_case = select(condition, true_case, false_case);
    }
    return py::cast(false_case);
}

py::object py_select(const py::args &args) {
    if (args.size() < 3) {
        throw py::value_error("select() must have at least 3 arguments");
    }

    if (args.size() % 2 != 1) {
        throw py::value_error("select() must have an odd number of arguments");
    }

    if (is_expr(args[0])) {        // If the condition is an Expr, then ...
        return is_expr(args[1]) ?  // ... we need to check the value's kind.
                   py_select_reduce<Expr, Expr>(args) :
                   py_select_reduce<Expr, Tuple>(args);
    }
    return py_select_reduce<Tuple, Tuple>(args);  // Otherwise, the value must be a tuple, too.
}

}  // namespace

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

    m.def("select", py_select);

    m.def("mux", static_cast<Expr (*)(const Expr &, const std::vector<Expr> &)>(&mux));
    m.def("mux", static_cast<Expr (*)(const Expr &, const Tuple &)>(&mux));
    m.def("mux", static_cast<Tuple (*)(const Expr &, const std::vector<Tuple> &)>(&mux));

    m.def("sin", &sin);
    m.def("asin", &asin);
    m.def("cos", &cos);
    m.def("acos", &acos);
    m.def("tan", &tan);
    m.def("atan", &atan);
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
    m.def("fma", &fma);
    m.def("fast_sin", &fast_sin);
    m.def("fast_cos", &fast_cos);
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
    m.def("reinterpret", static_cast<Expr (*)(Type, Expr)>(&reinterpret));
    m.def("cast", static_cast<Expr (*)(Type, Expr)>(&cast));

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
    m.def("random_float", [] { return random_float(); });
    m.def("random_float", &random_float, py::arg("seed"));
    m.def("random_uint", [] { return random_uint(); });
    m.def("random_uint", &random_uint, py::arg("seed"));
    m.def("random_int", [] { return random_int(); });
    m.def("random_int", &random_int, py::arg("seed"));
    m.def("undef", static_cast<Expr (*)(Type)>(&undef));

    m.def(
        "memoize_tag", [](const Expr &result, const py::args &cache_key_values) -> Expr {
            return Internal::memoize_tag_helper(result, args_to_vector<Expr>(cache_key_values));
        },
        py::arg("result"));

    m.def("likely", &likely);
    m.def("likely_if_innermost", &likely_if_innermost);
    m.def("saturating_cast", static_cast<Expr (*)(Type, Expr)>(&saturating_cast));
    m.def("strict_float", &strict_float);
    m.def("scatter", static_cast<Expr (*)(const std::vector<Expr> &)>(&scatter));
    m.def("gather", static_cast<Expr (*)(const std::vector<Expr> &)>(&gather));
    m.def("extract_bits", static_cast<Expr (*)(Type, const Expr &, const Expr &)>(&extract_bits));
    m.def("concat_bits", &concat_bits);
    m.def("widen_right_add", &widen_right_add);
    m.def("widen_right_mul", &widen_right_mul);
    m.def("widen_right_sub", &widen_right_sub);
    m.def("widening_add", &widening_add);
    m.def("widening_mul", &widening_mul);
    m.def("widening_sub", &widening_sub);
    m.def("widening_shift_left", static_cast<Expr (*)(Expr, int)>(&widening_shift_left));
    m.def("widening_shift_left", static_cast<Expr (*)(Expr, Expr)>(&widening_shift_left));
    m.def("widening_shift_right", static_cast<Expr (*)(Expr, int)>(&widening_shift_right));
    m.def("widening_shift_right", static_cast<Expr (*)(Expr, Expr)>(&widening_shift_right));
    m.def("rounding_shift_left", static_cast<Expr (*)(Expr, int)>(&rounding_shift_left));
    m.def("rounding_shift_left", static_cast<Expr (*)(Expr, Expr)>(&rounding_shift_left));
    m.def("rounding_shift_right", static_cast<Expr (*)(Expr, int)>(&rounding_shift_right));
    m.def("rounding_shift_right", static_cast<Expr (*)(Expr, Expr)>(&rounding_shift_right));
    m.def("saturating_add", &saturating_add);
    m.def("saturating_sub", &saturating_sub);
    m.def("halving_add", &halving_add);
    m.def("rounding_halving_add", &rounding_halving_add);
    m.def("halving_sub", &halving_sub);
    m.def("mul_shift_right", static_cast<Expr (*)(Expr, Expr, int)>(&mul_shift_right));
    m.def("mul_shift_right", static_cast<Expr (*)(Expr, Expr, Expr)>(&mul_shift_right));
    m.def("rounding_mul_shift_right", static_cast<Expr (*)(Expr, Expr, int)>(&rounding_mul_shift_right));
    m.def("rounding_mul_shift_right", static_cast<Expr (*)(Expr, Expr, Expr)>(&rounding_mul_shift_right));
    m.def("target_arch_is", &target_arch_is);
    m.def("target_bits", &target_bits);
    m.def("target_has_feature", &target_has_feature);
    m.def("target_natural_vector_size", static_cast<Expr (*)(Type)>(&target_natural_vector_size));
    m.def("target_os_is", &target_os_is);
    m.def("logical_not", [](const Expr &expr) -> Expr { return !expr; });
}

}  // namespace PythonBindings
}  // namespace Halide

#ifndef HALIDE_PYTHON_BINDINGS_PYFUNC_REF_H
#define HALIDE_PYTHON_BINDINGS_PYFUNC_REF_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

struct UnevaluatedFuncRefExpr {
    FuncRef lhs;
    Expr rhs;
    enum class Op {
        Add,
        Sub,
        Mul,
        Div,
    } op;

    template<typename T>
    void define_update(const Func &func, const T &args) {
        const Internal::Function &f = func.function();
        if (!f.same_as(lhs.function())) {
            std::stringstream ss;
            ss << "Cannot use an unevaluated reference to '" << lhs.function().name()
               << "' to define an update in a different func '" << func.name() << "'.";
            throw CompileError(ss.str());
        }
        if (!validate_args(lhs.get_args(), args)) {
            std::stringstream ss;
            ss << "Cannot use an unevaluated reference to '" << lhs.function().name()
               << "' to define an update with different arguments.";
            throw CompileError(ss.str());
        }
        if (f.has_pure_definition() || f.has_extern_definition()) {
            std::stringstream ss;
            ss << "Cannot use an unevaluated reference to '" << lhs.function().name()
               << "' to define an update when a pure definition already exists.";
            throw CompileError(ss.str());
        }
        switch (op) {
        case Op::Add:
            return (void)(lhs += rhs);
        case Op::Sub:
            return (void)(lhs -= rhs);
        case Op::Mul:
            return (void)(lhs *= rhs);
        case Op::Div:
            return (void)(lhs /= rhs);
        }
        throw InternalError("Invalid Op value.");
    }

private:
    template<typename T>
    static bool validate_args_impl(const std::vector<Expr> &expected, const std::vector<T> &actual) {
        if (expected.size() != actual.size()) {
            return false;
        }
        for (size_t i = 0; i < expected.size(); i++) {
            if (!Internal::equal(expected[i], actual[i])) {
                return false;
            }
        }
        return true;
    }

    template<typename T,
             typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, std::vector<std::decay_t<T>>>>>
    bool validate_args(const std::vector<Expr> &expected, T &&actual) {
        return validate_args_impl(expected, std::vector<std::decay_t<T>>{std::forward<T>(actual)});
    }

    template<typename T>
    bool validate_args(const std::vector<Expr> &expected, const std::vector<T> &actual) {
        return validate_args_impl(expected, actual);
    }
};
void define_func_ref(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYFUNC_REF_H

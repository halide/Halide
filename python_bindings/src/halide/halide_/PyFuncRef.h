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

    void define_update(const FuncRef &actual_ref) {
        if (!lhs.equivalent_to(actual_ref)) {
            std::stringstream ss;
            ss << "Cannot use an unevaluated reference to '" << lhs.function().name()
               << "' to define an update at a different location.";
            throw CompileError(ss.str());
        }
        const Internal::Function &f = actual_ref.function();
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
};
void define_func_ref(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYFUNC_REF_H

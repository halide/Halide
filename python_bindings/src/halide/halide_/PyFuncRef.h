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

    void define_update(const Func &func) {
        const Internal::Function &f = func.function();
        if (!f.same_as(lhs.function())) {
            std::stringstream ss;
            ss << "Cannot use an unevaluated reference to '" << lhs.function().name()
               << "' to define an update in a different func '" << func.name() << "'.";
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
};
void define_func_ref(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYFUNC_REF_H

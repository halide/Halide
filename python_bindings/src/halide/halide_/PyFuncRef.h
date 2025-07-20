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

    operator Expr() const {
        switch (op) {
        case Op::Add:
            return lhs + rhs;
        case Op::Sub:
            return lhs - rhs;
        case Op::Mul:
            return lhs * rhs;
        case Op::Div:
            return lhs / rhs;
        }
        throw std::runtime_error("Invalid operator");
    }

    bool define_update(const Func &func) {
        const Internal::Function &f = func.function();
        if (f.same_as(lhs.function()) && !(f.has_pure_definition() || f.has_extern_definition())) {
            switch (op) {
            case Op::Add:
                return lhs += rhs, true;
            case Op::Sub:
                return lhs -= rhs, true;
            case Op::Mul:
                return lhs *= rhs, true;
            case Op::Div:
                return lhs /= rhs, true;
            }
            throw std::runtime_error("Invalid operator");
        }
        return false;
    }
};
void define_func_ref(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYFUNC_REF_H

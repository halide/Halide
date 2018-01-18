#include "PyLambda.h"

namespace Halide {
namespace PythonBindings {

void define_lambda(py::module &m) {
    // TODO: 'lambda' is a reserved word in Python, so we
    // can't use it for a function. Using 'lambda_func' for now.
    m.def("lambda_func", [](py::args args) -> Func {
        auto vars = args_to_vector<Var>(args, 0, 1);
        Expr e = args[args.size() - 1].cast<Expr>();
        Func f("lambda" + Internal::unique_name('_'));
        f(vars) = e;
        return f;
    });
}

}  // namespace PythonBindings
}  // namespace Halide

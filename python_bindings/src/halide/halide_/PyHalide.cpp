#include "PyHalide.h"

#include "PyArgument.h"
#include "PyBoundaryConditions.h"
#include "PyBuffer.h"
#include "PyCallable.h"
#include "PyConciseCasts.h"
#include "PyDerivative.h"
#include "PyEnums.h"
#include "PyError.h"
#include "PyExpr.h"
#include "PyExternFuncArgument.h"
#include "PyFunc.h"
#include "PyGenerator.h"
#include "PyIROperator.h"
#include "PyImageParam.h"
#include "PyInlineReductions.h"
#include "PyLambda.h"
#include "PyModule.h"
#include "PyParam.h"
#include "PyPipeline.h"
#include "PyRDom.h"
#include "PyTarget.h"
#include "PyTuple.h"
#include "PyType.h"
#include "PyVar.h"

static_assert(PYBIND11_VERSION_MAJOR == 2 && PYBIND11_VERSION_MINOR >= 6,
              "Halide requires PyBind 2.6+");

static_assert(PY_VERSION_HEX >= 0x03000000,
              "We appear to be compiling against Python 2.x rather than 3.x, which is not supported.");

#ifndef HALIDE_PYBIND_MODULE_NAME
#define HALIDE_PYBIND_MODULE_NAME halide_
#endif

PYBIND11_MODULE(HALIDE_PYBIND_MODULE_NAME, m) {
    using namespace Halide::PythonBindings;

    // Order of definitions matters somewhat:
    // things used for default arguments must be registered
    // prior to that usage.
    define_enums(m);
    define_target(m);
    define_expr(m);
    define_tuple(m);
    define_argument(m);
    define_boundary_conditions(m);
    define_buffer(m);
    define_concise_casts(m);
    define_error(m);
    define_extern_func_argument(m);
    define_var(m);
    define_rdom(m);
    define_module(m);
    define_callable(m);
    define_func(m);
    define_pipeline(m);
    define_inline_reductions(m);
    define_lambda(m);
    define_operators(m);
    define_param(m);
    define_image_param(m);
    define_type(m);
    define_derivative(m);
    define_generator(m);

    // There is no PyUtil yet, so just put this here
    m.def("load_plugin", &Halide::load_plugin, py::arg("lib_name"));
}

namespace Halide {
namespace PythonBindings {

Expr double_to_expr_check(double v) {
    float f = static_cast<float>(v);
    double check = static_cast<double>(f);
    // 2^(-n) (or some combination) case is safe. e.g. 0.5, 0.25, 0.75, ...
    // otherwise, precision will be lost.  e.g. 0.1, 0.3, ...
    using Internal::reinterpret_bits;
    if (reinterpret_bits<uint64_t>(v) != reinterpret_bits<uint64_t>(check)) {
        std::ostringstream oss;
        oss.precision(17);
        oss << std::fixed << v;
        PyErr_WarnEx(
            PyExc_RuntimeWarning,
            ("The floating-point value " +
             oss.str() +
             " will be interpreted as a float32 by Halide and lose precision;"
             " add an explicit `f32()` or `f64()`` cast to avoid this warning.")
                .c_str(),
            0);
    }
    return Expr(f);
}

std::vector<Expr> collect_print_args(const py::args &args) {
    std::vector<Expr> v;
    v.reserve(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        // No way to see if a cast will work: just have to try
        // and fail. Normally we don't want string to be convertible
        // to Expr, but in this unusual case we do.
        try {
            v.emplace_back(args[i].cast<std::string>());
        } catch (...) {
            v.push_back(args[i].cast<Expr>());
        }
    }
    return v;
}

}  // namespace PythonBindings
}  // namespace Halide

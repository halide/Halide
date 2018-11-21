#include "PyHalide.h"

#include "PyArgument.h"
#include "PyBoundaryConditions.h"
#include "PyBuffer.h"
#include "PyConciseCasts.h"
#include "PyEnums.h"
#include "PyError.h"
#include "PyExpr.h"
#include "PyExternFuncArgument.h"
#include "PyFunc.h"
#include "PyIROperator.h"
#include "PyImageParam.h"
#include "PyInlineReductions.h"
#include "PyLambda.h"
#include "PyMachineParams.h"
#include "PyModule.h"
#include "PyOutputs.h"
#include "PyParam.h"
#include "PyPipeline.h"
#include "PyRDom.h"
#include "PyTarget.h"
#include "PyTuple.h"
#include "PyType.h"
#include "PyVar.h"

#ifndef HALIDE_PYBIND_MODULE_NAME
  #define HALIDE_PYBIND_MODULE_NAME halide
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
    define_outputs(m);
    define_machine_params(m);
    define_module(m);
    define_func(m);
    define_pipeline(m);
    define_inline_reductions(m);
    define_lambda(m);
    define_operators(m);
    define_param(m);
    define_image_param(m);
    define_type(m);
}

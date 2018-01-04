
#include "PyArgument.h"
#include "PyBoundaryConditions.h"
#include "PyError.h"
#include "PyExpr.h"
#include "PyFunc.h"
#include "PyFunction.h"
#include "PyIROperator.h"
#include "PyBuffer.h"
#include "PyInlineReductions.h"
#include "PyLambda.h"
#include "PyParam.h"
#include "PyRDom.h"
#include "PyTarget.h"
#include "PyType.h"
#include "PyVar.h"

BOOST_PYTHON_MODULE(halide) {
    using namespace Halide::PythonBindings;

    define_argument();
    define_boundary_conditions();
    define_buffer();
    define_error();
    define_expr();
    define_extern_func_argument();
    define_func();
    define_inline_reductions();
    define_lambda();
    define_operators();
    define_param();
    define_rdom();
    define_target();
    define_type();
    define_var();
}

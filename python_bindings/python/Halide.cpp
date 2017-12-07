#include <boost/python.hpp>

#include "Argument.h"
#include "BoundaryConditions.h"
#include "Error.h"
#include "Expr.h"
#include "Func.h"
#include "Function.h"
#include "IROperator.h"
#include "Image.h"
#include "InlineReductions.h"
#include "Lambda.h"
#include "Param.h"
#include "RDom.h"
#include "Target.h"
#include "Type.h"
#include "Var.h"

BOOST_PYTHON_MODULE(halide) {
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

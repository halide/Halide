#include <boost/python.hpp>

#include "Var.h"
#include "Expr.h"
#include "Func.h"
#include "Param.h"
#include "Type.h"
#include "IROperator.h"
#include "Argument.h"
#include "BoundaryConditions.h"
#include "Image.h"
#include "Buffer.h"
#include "Error.h"
#include "Target.h"

#include <stdexcept>
#include <vector>

char const* greet()
{
    return "hello, world from Halide python bindings";
}

BOOST_PYTHON_MODULE(halide)
{
    using namespace boost::python;
    def("greet", greet);

    // we include all the pieces and bits from the Halide API
    defineVar();
    defineExpr();
    defineFunc();
    defineType();
    defineParam();
    defineOperators();
    defineArgument();
    defineBoundaryConditions();
    defineImage();
    defineBuffer();
    defineError();
    defineTarget();

}

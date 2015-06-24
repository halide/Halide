#include <boost/python.hpp>

#include "Argument.h"
#include "BoundaryConditions.h"
#include "Buffer.h"
#include "Error.h"
#include "Expr.h"
#include "Func.h"
#include "Image.h"
#include "InlineReductions.h"
#include "IROperator.h"
#include "Lambda.h"
#include "Param.h"
#include "RDom.h"
#include "Target.h"
#include "Tuple.h"
#include "Type.h"
#include "Var.h"

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
    defineArgument();
    defineBoundaryConditions();
    defineBuffer();
    defineError();
    defineExpr();
    defineFunc();
    defineImage();
    defineInlineReductions();
    defineLambda();
    defineOperators();
    defineParam();
    defineRDom();
    defineTarget();
    defineTuple();
    defineType();
    defineVar();

}

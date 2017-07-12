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

//#include "llvm-3.5/llvm/Support/DynamicLibrary.h"

#include <stdexcept>
#include <vector>

/*
bool load_library_into_llvm(std::string name)
{
    return llvm::sys::DynamicLibrary::LoadLibraryPermanently(name.c_str());
}

void defineLlvmHelpers()
{
    using namespace boost::python;
    def("load_library_into_llvm", load_library_into_llvm,
        "This function permanently loads the dynamic library at the given path. "
        "It is safe to call this function multiple times for the same library.");

    return;
}
*/

BOOST_PYTHON_MODULE(halide) {
    using namespace boost::python;

    // we include all the pieces and bits from the Halide API
    defineArgument();
    defineBoundaryConditions();
    defineBuffer();
    defineError();
    defineExpr();
    defineExternFuncArgument();
    defineFunc();
    defineInlineReductions();
    defineLambda();
    defineOperators();
    defineParam();
    defineRDom();
    defineTarget();
    defineType();
    defineVar();

    // not part of the C++ Halide API
    //defineLlvmHelpers();
}

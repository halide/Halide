#ifndef HALIDE_MATLAB_OUTPUT_H
#define HALIDE_MATLAB_OUTPUT_H

/** \file
 *
 * Provides an output function to generate a Matlab mex API compatible object file.
 */

#include "Module.h"

namespace llvm {
class Module;
class Function;
}

namespace Halide {
namespace Internal {

/** Add a mexFunction wrapper definition to the module, calling the
 * function with the name pipeline_name. Returns the mexFunction
 * definition. */
EXPORT llvm::Function *define_matlab_wrapper(llvm::Module *module, const std::string &pipeline_name);

}
}

#endif

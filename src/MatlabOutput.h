#ifndef HALIDE_MATLAB_OUTPUT_H
#define HALIDE_MATLAB_OUTPUT_H

/** \file
 *
 * Provides an output function to generate a Matlab mex API compatible object file.
 */

#include "Module.h"

namespace Halide {

EXPORT void compile_module_to_matlab_mex(const Module &module, const std::string &pipeline_name);

}

#endif

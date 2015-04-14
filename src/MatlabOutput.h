#ifndef HALIDE_MATLAB_OUTPUT_H
#define HALIDE_MATLAB_OUTPUT_H

/** \file
 *
 * Provides an output function to generate a Matlab mex API compatible object file.
 */

#include "Module.h"

namespace Halide {

/** Compile a module to an object file suitable for use with Matlab's mex feature. */
EXPORT void compile_module_to_matlab_object(const Module &module, const std::string &pipeline_name,
                                            const std::string &filename);

}

#endif

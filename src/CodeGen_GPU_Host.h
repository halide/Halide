#ifndef HALIDE_CODEGEN_GPU_HOST_H
#define HALIDE_CODEGEN_GPU_HOST_H

/** \file
 * Defines the code-generator for producing GPU host code
 */

#include "CodeGen_ARM.h"
#include "CodeGen_X86.h"

namespace Halide {
namespace Internal {

struct CodeGen_GPU_Dev;

// Generate class declarations for each host target
#define GPU_HOST_TARGET X86
#include "CodeGen_GPU_Host_Template.h"
#undef GPU_HOST_TARGET

#define GPU_HOST_TARGET ARM
#include "CodeGen_GPU_Host_Template.h"
#undef GPU_HOST_TARGET

}}

#endif

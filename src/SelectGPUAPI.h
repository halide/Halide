#ifndef HALIDE_INTERNAL_SELECT_GPU_API_H
#define HALIDE_INTERNAL_SELECT_GPU_API_H

#include "IR.h"
#include "Target.h"

/** \file
 * Defines a lowering pass that selects which GPU api to use for each
 * gpu for loop
 */

namespace Halide {
namespace Internal {

/** If device_api is not Default_GPU, return the best GPU API to use
 * given for target. Currently chooses the first of the following:
 * Metal, OpenCL, CUDA, OpenGLCompute, Renderscript, OpenGL and
 * respects the target Texture flag for Metal and OpenCL generating
 * MetalTextures and OpenCLTextures respectively. If must_be_compute
 * is true, an error is generated if the result is not Metal, OpenCL,
 * CUDA, or OpenGLCompute. */
DeviceAPI fixup_device_api(DeviceAPI device_api, const Target &target,
			   bool must_be_compute = false);

/** Replace for loops with GPU_Default device_api with an actual
 * device API depending on what's enabled in the target. Choose the
 * first of the following: opencl, cuda, openglcompute, renderscript,
 * opengl */
Stmt select_gpu_api(Stmt s, Target t);

}
}


#endif

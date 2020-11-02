#ifndef HALIDE_ALIGN_GPU_BUFFERS_H
#define HALIDE_ALIGN_GPU_BUFFERS_H

/** \file
 * Defines the lowering passes that deal with host and device buffer flow.
 */

#include <string>
#include <vector>

#include "Expr.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Inject calls to halide_device_malloc, halide_copy_to_device, and
 * halide_copy_to_host as needed. */
Stmt align_gpu_buffers(Stmt s, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif

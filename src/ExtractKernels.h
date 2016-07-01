#ifndef HALIDE_EXTRACT_KERNELS_H
#define HALIDE_EXTRACT_KERNELS_H

/** \file Defines a lowering pass to pull loops marked with
 * device APIs to separate modules, and call them through the
 * host runtime module.
 */

#include "Module.h"

namespace Halide {
namespace Internal {

/** Pull loops marked with device APIs to a separate modules, and call them
 * through the host runtime module. */
Stmt extract_device_kernels(Stmt s, const std::string &function_name,
						    const Target &host_target);

}
}

#endif

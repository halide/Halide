#ifndef HALIDE_AMDGPU_OFFLOAD_H
#define HALIDE_AMDGPU_OFFLOAD_H

/** \file
 * Defines a lowering pass to pull loops marked with the
 * Amdgpu device API to a separate module, and call them through the
 * Amdgpu host runtime module.
 */

#include "Module.h"

namespace Halide {
namespace Internal {

/** Pull loops marked with the AMDGPU device API to a separate
 * module, and call them through the Amdgpu host runtime module. */
Stmt inject_amdgpu_rpc(Stmt s, const Target &host_target, Module &module);

Buffer<uint8_t> compile_module_to_amdgpu_shared_object(const Module &device_code);

}
}

#endif

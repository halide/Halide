#ifndef HALIDE_HEXAGON_OFFLOAD_H
#define HALIDE_HEXAGON_OFFLOAD_H

/** \file
 * Defines a lowering pass to pull loops marked with the
 * Hexagon device API to a separate module, and call them through the
 * Hexagon host runtime module.
 */

#include "Module.h"

namespace Halide {
namespace Internal {

/** Pull loops marked with the Hexagon device API to a separate
 * module, and call them through the Hexagon host runtime module. */
Stmt inject_hexagon_rpc(Stmt s, const Target &host_target, Module &module);

Buffer<uint8_t> compile_module_to_hexagon_shared_object(const Module &device_code);

}  // namespace Internal
}  // namespace Halide

#endif

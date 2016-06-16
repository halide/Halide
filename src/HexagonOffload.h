#ifndef HALIDE_HEXAGON_OFFLOAD_H
#define HALIDE_HEXAGON_OFFLOAD_H

/** \file Defines a lowering pass to pull loops marked with the
 * Hexagon device API to a separate module, and call them through the
 * Hexagon host runtime module.
 */

#include "Module.h"

namespace Halide {
namespace Internal {

/** Pull loops marked with the Hexagon device API to a separate
 * module, and call them through the Hexagon host runtime module. */
Stmt inject_hexagon_rpc(Stmt s, const Target &host_target);

}
}

#endif

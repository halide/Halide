#ifndef HEXAGON_OFFLOAD_H
#define HEXAGON_OFFLOAD_H

/** \file
 * Defines the lowering pass that vectorizes loops marked as such
 */

#include "Module.h"

namespace Halide {
namespace Internal {

/* Lowering pass for Hexagon
 */
Stmt inject_hexagon_rpc(Stmt s, const Target &host_target);

}
}

#endif

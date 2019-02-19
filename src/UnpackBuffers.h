#ifndef HALIDE_UNPACK_BUFFERS_H
#define HALIDE_UNPACK_BUFFERS_H

/** \file
 * Defines the lowering pass that unpacks buffer arguments onto the symbol table
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Creates let stmts for the various buffer components
 * (e.g. foo.extent.0) in any referenced concrete buffers or buffer
 * parameters. After this pass, the only undefined symbols should
 * scalar parameters and the buffers themselves (e.g. foo.buffer). */
Stmt unpack_buffers(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif

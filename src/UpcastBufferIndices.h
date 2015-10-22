#ifndef HALIDE_UPCAST_BUFFER_INDICES_H
#define HALIDE_UPCAST_BUFFER_INDICES_H

/** \file
 * Defines the lowering pass for upcasting buffer indices on 64-bit targets.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Upcasts load and store indices to 64 bits. */
Stmt upcast_buffer_indices(Stmt s);

}
}

#endif

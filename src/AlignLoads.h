#ifndef HALIDE_ALIGN_LOADS_H
#define HALIDE_ALIGN_LOADS_H

/** \file Defines a lowering pass that rewrites unaligned loads into
 * sequences of aligned loads.
 */
#include "IR.h"
#include "Target.h"
namespace Halide {
namespace Internal {

Stmt align_loads(Stmt s, int alignment);

}
}

#endif

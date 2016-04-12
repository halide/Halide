#ifndef HALIDE_HEXAGON_ALIGN_LOADS_H
#define HALIDE_HEXAGON_ALIGN_LOADS_H

/** \file
 * Defines a lowering pass the breaks unaligned loads into two aligned
 * loads for Hexagon (HVX).
 */

#include <map>

#include "IR.h"
#include "Target.h"
namespace Halide {
namespace Internal {

    Stmt hexagon_align_loads(Stmt s, const Target &t);

}
}

#endif


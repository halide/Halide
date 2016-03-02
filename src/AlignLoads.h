#ifndef HALIDE_ALIGN_LOADS_H
#define HALIDE_ALIGN_LOADS_H

/** \file
 * Defines the lowering pass that flattens multi-dimensional storage
 * into single-dimensional array access
 */

#include <map>

#include "IR.h"
#include "Target.h"
namespace Halide {
namespace Internal {

    Stmt align_loads(Stmt s, const Target &t);

}
}

#endif


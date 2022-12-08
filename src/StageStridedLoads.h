#ifndef HALIDE_INTERNAL_STAGE_STRIDED_LOADS_H
#define HALIDE_INTERNAL_STAGE_STRIDED_LOADS_H

/** \file
 * TODO
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

/** For each strided load in a statement, look for evidence elsewhere that it's
 * safe to read a few elements later or earlier so that it can done as a dense
 * load followed by a shuffle. Additionally, the dense load is selected to try to
 * get groups of strided loads with small offsets from each other to share the
 * same dense load. E.g. if we have f[ramp(x, 2, 16)] + f[ramp(x+1, 2, 16)]) we
 * want to rewrite them to be a single dense load and a shuffle. */
Stmt stage_strided_loads(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif

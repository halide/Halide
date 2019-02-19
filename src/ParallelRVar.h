#ifndef HALIDE_PARALLEL_RVAR_H
#define HALIDE_PARALLEL_RVAR_H

/** \file
 *
 * Method for checking if it's safe to parallelize an update
 * definition across a reduction variable.
 */

#include "Function.h"

namespace Halide {
namespace Internal {

/** Returns whether or not Halide can prove that it is safe to
 * parallelize an update definition across a specific variable. If
 * this returns true, it's definitely safe. If this returns false, it
 * may still be safe, but Halide couldn't prove it.
 */
bool can_parallelize_rvar(const std::string &rvar,
                          const std::string &func,
                          const Definition &r);

}  // namespace Internal
}  // namespace Halide

#endif

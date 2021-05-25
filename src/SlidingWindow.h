#ifndef HALIDE_SLIDING_WINDOW_H
#define HALIDE_SLIDING_WINDOW_H

/** \file
 *
 * Defines the sliding_window lowering optimization pass, which avoids
 * computing provably-already-computed values.
 */

#include <map>
#include <string>

#include "Expr.h"

namespace Halide {
namespace Internal {

class Function;

/** Perform sliding window optimizations on a halide
 * statement. I.e. don't bother computing points in a function that
 * have provably already been computed by a previous iteration.
 */
Stmt sliding_window(const Stmt &s, const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif

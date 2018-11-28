#ifndef HALIDE_DEBUG_TO_FILE_H
#define HALIDE_DEBUG_TO_FILE_H

/** \file
 * Defines the lowering pass that injects code at the end of
 * every realization to dump functions to a file for debugging.  */

#include <map>

#include "IR.h"

namespace Halide {
namespace Internal {

/** Takes a statement with Realize nodes still unlowered. If the
 * corresponding functions have a debug_file set, then inject code
 * that will dump the contents of those functions to a file after the
 * realization. */
Stmt debug_to_file(Stmt s,
                   const std::vector<Function> &outputs,
                   const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif

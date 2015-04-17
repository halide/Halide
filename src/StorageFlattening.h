#ifndef HALIDE_STORAGE_FLATTENING_H
#define HALIDE_STORAGE_FLATTENING_H

/** \file
 * Defines the lowering pass that flattens multi-dimensional storage
 * into single-dimensional array access
 */

#include <map>

#include "IR.h"

namespace Halide {
namespace Internal {

/** Take a statement with multi-dimensional Realize, Provide, and Call
 * nodes, and turn it into a statement with single-dimensional
 * Allocate, Store, and Load nodes respectively. */
Stmt storage_flattening(Stmt s, const std::string &output,
                        const std::map<std::string, Function> &env);

}
}

#endif

#ifndef HALIDE_INTERNAL_STORE_WITH
#define HALIDE_INTERNAL_STORE_WITH

#include <map>

#include "IR.h"

/** \file
 * Defines the lowering pass that implements the store_with scheduling
 * directive. Internally uses some generic polyhedral optimization
 * machinery, which should be exposed in a separate module if it
 * becomes useful elsewhere in the compiler. */

namespace Halide {
namespace Internal {

/** Implements store_with scheduling directives by merging buffers
 * according to the directives, proving that this doesn't change the
 * meaning of the algorithm. Throws user errors in a wide variety of
 * situations in which use of store_with would produce a different
 * output. */
Stmt lower_store_with(const Stmt &s,
                      const std::vector<Function> &outputs,
                      const std::map<std::string, Function> &env);

}
}

#endif

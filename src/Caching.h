#ifndef HALIDE_INTERNAL_CACHING_H
#define HALIDE_INTERNAL_CACHING_H

/** \file
 *
 * Defines the interface to the pass that injects support for
 * compute_cached roots.
 */

#include <map>
#include "IR.h"
#include "Target.h"

namespace Halide {
namespace Internal {

  Stmt inject_caching(Stmt s, const std::map<std::string, Function> &env);

}
}

#endif

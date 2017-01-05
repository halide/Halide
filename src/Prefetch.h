#ifndef HALIDE_PREFETCH_H
#define HALIDE_PREFETCH_H

/** \file
 * Defines the lowering pass that injects prefetch calls when prefetching
 * appears in the schedule.
 */

#include <map>

#include "IR.h"
#include "Target.h"

namespace Halide {
namespace Internal {

Stmt inject_prefetch(Stmt s, const std::map<std::string, Function> &env);

}
}

#endif

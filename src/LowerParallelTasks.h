#ifndef HALIDE_LOWER_PARALLEL_TASKS_H
#define HALIDE_LOWER_PARALLEL_TASKS_H

/** \file
 *
 * Support for platform independent lowering of Halide parallel and async mechanisms.
 * May eventually become a lowering pass.
 */

#include "IRVisitor.h"

namespace Halide {
namespace Internal {

Stmt lower_parallel_tasks(const Stmt &s, std::vector<LoweredFunc> &closure_implementations,
                          const std::string &name, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_LOWER_PARALLEL_TASKS_H

#ifndef HALIDE_ASYNC_PRODUCERS_H
#define HALIDE_ASYNC_PRODUCERS_H

/** \file
 * Defines the lowering pass that injects task parallelism for producers that are scheduled as async.
 */
#include <map>
#include <string>

#include "Expr.h"

namespace Halide {
namespace Internal {

class Function;

Stmt fork_async_producers(Stmt s, const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif

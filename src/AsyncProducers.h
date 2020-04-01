#ifndef HALIDE_ASYNC_PRODUCERS_H
#define HALIDE_ASYNC_PRODUCERS_H

#include <map>
#include <string>

#include "Expr.h"
/** \file
 * Defines the lowering pass that injects task parallelism for producers that are scheduled as async.
 */

namespace Halide {
namespace Internal {
class Function;

Stmt fork_async_producers(Stmt s, const std::map<std::string, Function> &env);

}
}  // namespace Halide

#endif

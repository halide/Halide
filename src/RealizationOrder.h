#ifndef HALIDE_INTERNAL_REALIZATION_ORDER_H
#define HALIDE_INTERNAL_REALIZATION_ORDER_H

/** \file
 *
 * Defines the lowering pass that determines the order in which
 * realizations are injected.
 */

#include <vector>
#include <string>
#include <map>

namespace Halide {
namespace Internal {

class Function;

/** Given a bunch of functions that call each other, determine an
 * order in which to do the scheduling. This in turn influences the
 * order in which stages are computed when there's no strict
 * dependency between them. Currently just some arbitrary depth-first
 * traversal of the call graph. */
std::vector<std::string> realization_order(const Function &output,
                                           const std::map<std::string, Function> &env);

}
}

#endif

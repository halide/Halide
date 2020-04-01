#ifndef HALIDE_SPLIT_TUPLES_H
#define HALIDE_SPLIT_TUPLES_H

#include <map>
#include <string>

#include "Expr.h"

/** \file
 * Defines the lowering pass that breaks up Tuple-valued realization
 * and productions into several scalar-valued ones. */

namespace Halide {
namespace Internal {
class Function;

/** Rewrite all tuple-valued Realizations, Provide nodes, and Call
 * nodes into several scalar-valued ones, so that later lowering
 * passes only need to think about scalar-valued productions. */

Stmt split_tuples(const Stmt &s, const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif

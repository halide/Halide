#ifndef HALIDE_SPLIT_TUPLES_H
#define HALIDE_SPLIT_TUPLES_H

#include "Expr.h"
#include "Function.h"
#include <map>

/** \file
 * Defines the lowering pass that breaks up Tuple-valued realization
 * and productions into several scalar-valued ones. */

namespace Halide {
namespace Internal {

/** Rewrite all tuple-valued Realizations, Provide nodes, and Call
 * nodes into several scalar-valued ones, so that later lowering
 * passes only need to think about scalar-valued productions. */

Stmt split_tuples(Stmt s, const std::map<std::string, Function> &env);

}  // namespace Internal
}  // namespace Halide

#endif

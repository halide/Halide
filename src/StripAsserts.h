#ifndef HALIDE_STRIP_ASSERTS_H
#define HALIDE_STRIP_ASSERTS_H

/** \file
 * Defines the lowering pass that strips asserts when NoAsserts is set.
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

Stmt strip_asserts(const Stmt &s);

}  // namespace Internal
}  // namespace Halide

#endif

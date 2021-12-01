#ifndef HALIDE_CODEGEN_HEXAGON_H
#define HALIDE_CODEGEN_HEXAGON_H

#include "Expr.h"

namespace Halide {

struct Target;

namespace Internal {

Stmt inject_hvx_lock_unlock(Stmt body, const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif

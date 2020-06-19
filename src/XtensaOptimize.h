#ifndef HALIDE_XTENSA_OPTIMIZE_H
#define HALIDE_XTENSA_OPTIMIZE_H

#include "Expr.h"

namespace Halide {
namespace Internal {

Stmt match_xtensa_patterns(Stmt);

}  // namespace Internal
}  // namespace Halide

#endif

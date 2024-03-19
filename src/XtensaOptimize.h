#ifndef HALIDE_XTENSA_OPTIMIZE_H
#define HALIDE_XTENSA_OPTIMIZE_H

#include "Expr.h"

namespace Halide {

struct Target;

namespace Internal {

std::string suffix_for_type(Type t);

Stmt match_xtensa_patterns(const Stmt &s, const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif

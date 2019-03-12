#ifndef HALIDE_SIMPLIFY_CORRELATED_DIFFERENCES
#define HALIDE_SIMPLIFY_CORRELATED_DIFFERENCES

#include "IR.h"

// TODO: blurb about interval arithmetic

namespace Halide {
namespace Internal {

Stmt simplify_correlated_differences(const Stmt &);

}
}

#endif

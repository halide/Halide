#ifndef HALIDE_SLIDING_WINDOW_H
#define HALIDE_SLIDING_WINDOW_H

#include "IR.h"

namespace Halide {
namespace Internal {

// Perform sliding window optimizations. I.e. don't bother computing
// points in a function that have provably already been computed by a
// previous iteration.
Stmt sliding_window(Stmt s, const map<string, Function> &env);

}
}

#endif

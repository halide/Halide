#ifndef HALIDE_VECTORIZE_LOOPS_H
#define HALIDE_VECTORIZE_LOOPS_H

#include "IR.h"

namespace Halide {
namespace Internal {

// Take a statement with for loops marked for vectorization, and turn
// them into single statements that operate on vectors.
Stmt vectorize_loops(Stmt);

}
}

#endif

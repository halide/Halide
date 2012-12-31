#ifndef HALIDE_TRACING_H
#define HALIDE_TRACING_H

#include "IR.h"

namespace Halide {
namespace Internal {

// Take a statement representing a halide pipeline, and (depending on
// the environment variable HL_TRACE), inject print statements at
// interesting points, such as allocations. Should be done before
// storage flattening, but after all bounds inference.

Stmt inject_tracing(Stmt);

}
}

#endif

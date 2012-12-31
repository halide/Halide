#ifndef HALIDE_STORAGE_FLATTENING_H
#define HALIDE_STORAGE_FLATTENING_H

#include "IR.h"

namespace Halide {
namespace Internal {

// Take a statement with multi-dimensional Realize, Provide, and Call
// nodes, and turn it into a statement with single-dimensional
// Allocate, Store, and Load nodes respectively.
Stmt do_storage_flattening(Stmt s);

}
}

#endif

#ifndef HALIDE_INJECT_OPENGL_INTRINSICS_H
#define HALIDE_INJECT_OPENGL_INTRINSICS_H

#include "IR.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

Stmt inject_opengl_intrinsics(Stmt s, Scope<int> &needs_buffer_t);

}
}

#endif

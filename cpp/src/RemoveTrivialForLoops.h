#ifndef HALIDE_REMOVE_TRIVIAL_FOR_LOOPS_H
#define HALIDE_REMOVE_TRIVIAL_FOR_LOOPS_H

#include "IR.h"

namespace Halide {
namespace Internal {

Stmt remove_trivial_for_loops(Stmt s);

}
}

#endif

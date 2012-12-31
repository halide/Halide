#ifndef REMOVE_DEAD_LETS_H
#define REMOVE_DEAD_LETS_H

#include "IR.h"

namespace Halide {
namespace Internal {

// Prune LetStmt and Let nodes that define variables that are never used
Stmt remove_dead_lets(Stmt s);

}
}

#endif

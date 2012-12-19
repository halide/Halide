#ifndef SUBSTITUTE_H
#define SUBSTITUTE_H

#include "IRMutator.h"

namespace Halide { 
namespace Internal {

/* Substitute an expression for a variable in a stmt or expr */
Expr substitute(string name, Expr replacement, Expr expr);
Stmt substitute(string name, Expr replacement, Stmt stmt);

}
}

#endif

#ifndef HALIDE_SUBSTITUTE_H
#define HALIDE_SUBSTITUTE_H

#include "IRMutator.h"

namespace Halide { 
namespace Internal {

/* Substitute an expression for a variable in a stmt or expr */
Expr substitute(std::string name, Expr replacement, Expr expr);
Stmt substitute(std::string name, Expr replacement, Stmt stmt);

}
}

#endif

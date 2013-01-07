#ifndef HALIDE_SIMPLIFY_H
#define HALIDE_SIMPLIFY_H

#include "IR.h"

namespace Halide { 
namespace Internal {

/* Perform a a wide range of simplifications to expressions
 * and statements, including constant folding, substituting in
 * trivial values, arithmetic rearranging, etc.
 */
Stmt simplify(Stmt);
Expr simplify(Expr);
        
void simplify_test();

}
}

#endif

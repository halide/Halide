#ifndef HALIDE_IR_EQUALITY_H
#define HALIDE_IR_EQUALITY_H

/** \file
 * Methods to test Exprs and Stmts for equality of value
 */

#include "IR.h"

namespace Halide { 
namespace Internal {

/** Compare expressions for equality of value. Traverses entire
 * expression tree. For equality of reference, use Expr::same_as */
bool equal(Expr a, Expr b);

/** Compare two statements for equality of value. Traverses entire
 * statement tree. For equality of reference, use Stmt::same_as */
bool equal(Stmt a, Stmt b);

}
}

#endif

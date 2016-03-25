#ifndef HALIDE_IR_ELIMINATE_BOOL_VECTORS_H
#define HALIDE_IR_ELIMINATE_BOOL_VECTORS_H

/** \file
 * Method to eliminate vectors of booleans from IR.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Some targets treat vectors of bools as integers of the same type
 * that the boolean operation is being used to operate on. For
 * example, instead of select(i1x8, u16x8, u16x8), the target would
 * prefer to see select(u16x8, u16x8, u16x8), where the first argument
 * is a vector of integers that are either all ones or all zeros. This
 * pass converts vectors of bools to vectors of integers to meet this
 * requirement. This is done by casting boolean vectors to integers
 * (with sign extension), using bitwise instead of logical operators,
 * and replacing the conditions of select with a not equals zero
 * expression. */
Stmt eliminate_bool_vectors(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif

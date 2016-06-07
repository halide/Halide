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
///@{
EXPORT Stmt eliminate_bool_vectors(Stmt s);
EXPORT Expr eliminate_bool_vectors(Expr s);
///@}

/** If a type is a boolean vector, find the type that it has been
 * changed to by eliminate_bool_vectors. */
EXPORT inline Type eliminated_bool_type(Type bool_type, Type other_type) {
    if (bool_type.is_vector() && bool_type.bits() == 1) {
        bool_type = bool_type.with_code(Type::Int).with_bits(other_type.bits());
    }
    return bool_type;
}

}  // namespace Internal
}  // namespace Halide

#endif

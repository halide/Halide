#ifndef HALIDE_ONE_TO_ONE_H
#define HALIDE_ONE_TO_ONE_H

/** \file
 *
 * Methods for determining if an Expr represents a one-to-one function
 * in its Variables.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Conservatively determine whether an integer expression is
 * one-to-one in its variables. For now this means it contains a
 * single variable and its derivative is provably strictly positive or
 * strictly negative. */
bool is_one_to_one(Expr expr);

void is_one_to_one_test();

}
}

#endif

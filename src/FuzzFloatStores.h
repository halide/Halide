#ifndef FUZZ_FLOAT_STORES_H
#define FUZZ_FLOAT_STORES_H

#include "Expr.h"

/** \file
 * Defines a lowering pass that messes with floating point stores.
 */

namespace Halide {
namespace Internal {

/** On every store of a floating point value, mask off the
 * least-significant-bit of the mantissa. We've found that whether or
 * not this dramatically changes the output of a pipeline correlates
 * very well with whether or not a pipeline will produce very
 * different outputs on different architectures (e.g. with and without
 * FMA). It's also a useful way to detect bad tests, such as those
 * that expect exact floating point equality across platforms. */
Stmt fuzz_float_stores(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif

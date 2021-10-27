#ifndef HANNK_TRANSFORMS_H
#define HANNK_TRANSFORMS_H

#include "interpreter/ops.h"

namespace hannk {

// Rewrites ops to be in-place operations when possible.
void in_place(Op *op);

// Remove ops that are unused.
void remove_dead_ops(OpGroup *op);

// Add pad ops before ops that need it, so those ops can
// assume everything needed of the input is in bounds.
// New ops will have prepare() called on them; this will return false
// if any of those calls fail.
[[nodiscard]] bool pad_for_ops(OpGroup *op);

// Execute ops that are constant, and mark the results
// constant as well.
void fold_constants(OpGroup *op);

}  // namespace hannk

#endif  // HANNK_TRANSFORMS_H
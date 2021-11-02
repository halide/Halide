#ifndef HANNK_TRANSFORMS_H
#define HANNK_TRANSFORMS_H

#include "interpreter/ops.h"

namespace hannk {

using OpGroupPtr = std::unique_ptr<OpGroup>;

// Rewrites ops to be in-place operations when possible.
[[nodiscard]] OpGroupPtr in_place(OpGroupPtr op);

// Remove ops that are unused.
[[nodiscard]] OpGroupPtr remove_dead_ops(OpGroupPtr op);

// Add pad ops before ops that need it, so those ops can
// assume everything needed of the input is in bounds.
// New ops will have prepare() called on them; this will return false
// if any of those calls fail.
[[nodiscard]] bool pad_for_ops(OpGroup *op);

// Execute ops that are constant, and mark the results
// constant as well.
[[nodiscard]] OpGroupPtr fold_constants(OpGroupPtr op);

}  // namespace hannk

#endif  // HANNK_TRANSFORMS_H

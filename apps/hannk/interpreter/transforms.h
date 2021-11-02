#ifndef HANNK_TRANSFORMS_H
#define HANNK_TRANSFORMS_H

#include "interpreter/ops.h"

namespace hannk {

// Rewrites ops to be in-place operations when possible.
[[nodiscard]] OpPtr in_place(OpPtr op);

// Remove ops that are unused.
[[nodiscard]] OpPtr remove_dead_ops(OpPtr op);

// Add pad ops before ops that need it, so those ops can
// assume everything needed of the input is in bounds.
// New ops will have prepare() called on them; this will return nullptr
// if any of those calls fail.
[[nodiscard]] OpPtr pad_for_ops(OpPtr op);

// Execute ops that are constant, and mark the results
// constant as well.
[[nodiscard]] OpPtr fold_constants(OpPtr op);

// Flatten all nested OpGroups into a single OpGroup.
// TODO: OpGroups that represent subgraphs shouldn't be flattened;
// this will need smartening when we represent subgraphs in hannk.
[[nodiscard]] OpPtr flatten_groups(OpPtr op);

// Some networks use padding already for other reasons, so
// we might have introduced two paddings in a row, which is
// a waste; this combines them. (This should be run after flatten_groups().)
[[nodiscard]] OpPtr fuse_pad_ops(OpPtr op);

}  // namespace hannk

#endif  // HANNK_TRANSFORMS_H

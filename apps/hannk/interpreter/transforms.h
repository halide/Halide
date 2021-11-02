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
// New ops will have prepare() called on them; this will return nullptr
// if any of those calls fail.
[[nodiscard]] OpGroupPtr pad_for_ops(OpGroupPtr op);

// Execute ops that are constant, and mark the results
// constant as well.
[[nodiscard]] OpGroupPtr fold_constants(OpGroupPtr op);

// Flatten all nested OpGroups into a single OpGroup.
// TODO: OpGroups that represent subgraphs shouldn't be flattened;
// this will need smartening when we represent subgraphs in hannk.
[[nodiscard]] OpGroupPtr flatten_groups(OpGroupPtr op);

// Some networks use padding already for other reasons, so
// we might have introduced two paddings in a row, which is
// a waste; this combines them. (This should be run after flatten_groups().)
[[nodiscard]] OpGroupPtr fuse_pad_ops(OpGroupPtr op);

}  // namespace hannk

#endif  // HANNK_TRANSFORMS_H

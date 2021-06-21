#ifndef HANNK_TRANSFORMS_H
#define HANNK_TRANSFORMS_H

#include "interpreter/ops.h"

namespace hannk {

struct AliasGroup {
    // The TensorDimensions and type-size used for the common allocation.
    TensorDimensions dimensions;
    size_t element_size_in_bytes;

    // Each Tensor that will share the storage, along with the
    // offset needed from the min
    std::vector<std::pair<TensorPtr, TensorOffset>> tensors;
};

// Calculates which Tensors can be aliased onto each other to allow for in-place operations.
// Doesn't actually mutate any tensors.
std::vector<AliasGroup> calculate_in_place_aliases(Op *op);

// Remove ops that are unused.
void remove_dead_ops(OpGroup *op);

// Add pad ops before ops that need it, so those ops can
// assume everything needed of the input is in bounds.
void pad_for_ops(OpGroup *op);

// Execute ops that are constant, and mark the results
// constant as well.
void fold_constants(OpGroup *op);

}  // namespace hannk

#endif  // HANNK_TRANSFORMS_H
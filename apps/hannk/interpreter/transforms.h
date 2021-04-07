#ifndef TRANSFORMS_H_
#define TRANSFORMS_H_

#include "interpreter/ops.h"

namespace hannk {

// Rewrites ops to be in-place operations when possible.
void in_place(Model *m);

// Remove ops that are unused.
void remove_dead_ops(Model *m);

// Add pad ops before ops that need it, so those ops an
// assume everything needed of the input is in bounds.
void pad_for_ops(Model *m);

// Execute ops that are constant, and mark the results
// constant as well.
void fold_constants(Model *m);

}  // namespace hannk

#endif  // TRANSFORMS_H_
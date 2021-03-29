#ifndef IN_PLACE_H_
#define IN_PLACE_H_

#include "interpreter/ops.h"

namespace hannk {

// Rewrites ops to be in-place operations when possible.
void in_place(Model *m);

// Remove ops that are unused.
void remove_dead_ops(Model *m);

// Add pad ops before conv and depthwise conv, so those
// ops an assume everything needed of the input is in
// bounds.
void pad_for_conv(Model *m);

// Remove pad ops when consuming ops don't need it.
void remove_pad_ops(Model *m);

}  // namespace hannk

#endif  // IN_PLACE_H_
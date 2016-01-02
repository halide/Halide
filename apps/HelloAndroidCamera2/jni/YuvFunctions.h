#ifndef YUV_FUNCTIONS_H
#define YUV_FUNCTIONS_H

#include <stdint.h>
#include "HalideRuntime.h"

#include "YuvBufferT.h"

// Returns true if a and b have the same extent.
bool equalExtents(const YuvBufferT &a, const YuvBufferT &b);

// Returns true if src and dst have the same extent and the copy was
// successful, false otherwise.
//
// Note that result == false can leave dst in an inconsistent state, with
// some sub-buffers copied and some not.
bool copy2D(const YuvBufferT &src, const YuvBufferT &dst);

#endif // YUV_FUNCTIONS_H

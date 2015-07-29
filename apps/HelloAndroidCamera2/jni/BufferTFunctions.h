#ifndef BUFFER_T_FUNCTIONS_H
#define BUFFER_T_FUNCTIONS_H

#include "HalideRuntime.h"

// Returns buf.host == nullptr.
bool isHostNull(const buffer_t &buf);

// Returns true if a and b have the same extent.
bool equalExtents(const buffer_t &a, const buffer_t &b);

// Returns true if a and b have the same stride.
bool equalStrides(const buffer_t &a, const buffer_t &b);

// Copy 2D buffer_t instances from src to dst, as long as they
// have the same extents and elem_size.
bool copy2D(const buffer_t &src, const buffer_t &dst);

// Fills a 2D buffer_t that has an elem_size of 1 with value.
bool fill2D(const buffer_t &buffer, uint8_t value);

#endif // BUFFER_T_FUNCTIONS_H

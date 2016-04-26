#ifndef HALIDE_RUNTIME_ION_H
#define HALIDE_RUNTIME_ION_H

#include "HalideRuntime.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Ion {

// Allocate an ION buffer, and map it, returning the mapped
// pointer. out_fd optionally contains the file descriptor of the ION
// buffer.
void *ion_alloc(void *user_context, size_t len, int heap_id, int *out_fd = NULL);

// Free a previously allocated ION buffer.
void ion_free(void *user_context, void *ion);

}}}}  // namespace Halide::Runtime::Internal::Ion

#endif

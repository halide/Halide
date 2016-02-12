#ifndef HALIDE_RUNTIME_ION_H
#define HALIDE_RUNTIME_ION_H

#include "runtime_internal.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Ion {

typedef int ion_user_handle_t;

int ion_open();
void ion_close(int ion_fd);

// Use ION_IOC_ALLOC to allocate a buffer.
ion_user_handle_t ion_alloc(int ion_fd, size_t len, size_t align, unsigned int heap_id_mask, unsigned int flags);
// Use ION_IOC_FREE to release a buffer.
void ion_free(int ion_fd, ion_user_handle_t ion_buffer);
// Use ION_IOC_MAP to map a buffer to a file descriptor.
int ion_map(int ion_fd, ion_user_handle_t ion_buffer);

}}}}  // namespace Halide::Runtime::Internal::Ion

#endif

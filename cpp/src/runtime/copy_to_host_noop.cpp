// Architectures that do not distinguish between device and host
// (i.e. not gpus), don't need a definition of copy_to_host

#include "../buffer_t.h"

#define WEAK __attribute__((weak))

extern "C" WEAK void halide_copy_to_host(buffer_t *) {}

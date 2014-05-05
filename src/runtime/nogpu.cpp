// Architectures that do not distinguish between device and host
// (i.e. not gpus), don't need a definition of copy_to_host
#include "HalideRuntime.h"
#include "mini_stdint.h"
#include "../buffer_t.h"

extern "C" WEAK int halide_copy_to_host(void *user_context, buffer_t *) {
    return 0;
}

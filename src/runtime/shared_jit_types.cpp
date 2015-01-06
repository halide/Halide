#include "runtime_internal.h"

#include "../buffer_t.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK void *halide_announce_buffer_t_type(struct buffer_t *buf) {
    return buf->host;
}

}}}


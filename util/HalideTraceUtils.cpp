#include "HalideTraceUtils.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace Halide {
namespace Internal {

bool Packet::read_from_stdin() {
    return read_from_filedesc(stdin);
}

bool Packet::read_from_filedesc(FILE *fdesc) {
    size_t header_size = sizeof(halide_trace_packet_t);
    if (!Packet::read(this, header_size, fdesc)) {
        return false;
    }
    size_t payload_size = size - header_size;
    if (payload_size > sizeof(payload)) {
        fprintf(stderr, "Payload larger than %d bytes in trace stream (%d)\n", (int)sizeof(payload), (int)payload_size);
        abort();
        return false;
    }
    if (!Packet::read(payload, payload_size, fdesc)) {
        fprintf(stderr, "Unexpected EOF mid-packet");
        return false;
    }
    return true;
}

bool Packet::read(void *d, size_t size, FILE *fdesc) {
    uint8_t *dst = (uint8_t *)d;
    if (!size) {
        return true;
    }
    size_t s = fread(dst, 1, size, fdesc);
    if (s != size) {
        if (ferror(fdesc) || !feof(fdesc)) {
            perror("Failed during read");
            exit(1);
        }
        return false;  // EOF
    }

    return true;
}

void bad_type_error(halide_type_t type) {
    fprintf(stderr, "Can't convert packet with type: %d bits: %d\n", type.code, type.bits);
    exit(1);
}

}  // namespace Internal
}  // namespace Halide

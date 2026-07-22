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
    if (size < header_size) {
        fprintf(stderr, "Packet smaller than the header in trace stream (%d)\n", (int)size);
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
    if (!validate_payload(payload_size)) {
        fprintf(stderr, "Malformed packet in trace stream\n");
        return false;
    }
    return true;
}

bool Packet::validate_payload(size_t payload_size) const {
    // dimensions, lanes and the type all come from the stream, and together
    // they say where the coordinates, the value, the func name and the trace
    // tag live in the payload. Check that layout fits in the bytes we read
    // before any of the accessors hand out a pointer into it.
    if (dimensions < 0) {
        return false;
    }
    size_t used = (size_t)dimensions * sizeof(int32_t) + value_bytes();
    if (used > payload_size) {
        return false;
    }
    // The func name and the trace tag follow the value, and both must be
    // nul-terminated within the payload.
    const uint8_t *p = payload + used;
    size_t remaining = payload_size - used;
    for (int i = 0; i < 2; i++) {
        const uint8_t *nul = (const uint8_t *)memchr(p, 0, remaining);
        if (!nul) {
            return false;
        }
        remaining -= nul + 1 - p;
        p = nul + 1;
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

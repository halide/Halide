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
        fprintf(stderr, "Malformed trace packet: size (%d) smaller than header\n", (int)size);
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
    // dimensions and type come straight from the untrusted header, and
    // coordinates()/value()/func()/trace_tag() use them to index into payload.
    // Reject any packet whose declared layout doesn't fit the bytes we read so
    // those accessors stay in bounds.
    if (dimensions < 0) {
        fprintf(stderr, "Malformed trace packet: negative dimensions (%d)\n", (int)dimensions);
        return false;
    }
    const size_t fixed_bytes = (size_t)dimensions * sizeof(int32_t) + value_bytes();
    if (fixed_bytes > payload_size) {
        fprintf(stderr, "Malformed trace packet: coordinates/value exceed payload\n");
        return false;
    }
    // func() and trace_tag() are NUL-terminated strings following the value;
    // both terminators must lie within the payload or the walks run past it.
    int terminators = 0;
    for (size_t i = fixed_bytes; i < payload_size; i++) {
        if (payload[i] == 0 && ++terminators == 2) {
            break;
        }
    }
    if (terminators < 2) {
        fprintf(stderr, "Malformed trace packet: func/trace_tag not terminated within payload\n");
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

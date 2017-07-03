#include "HalideTraceUtils.h"
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool Packet::read_from_stdin() {
    return read_from_filedesc(0);
}

bool Packet::read_from_filedesc(int fdesc){
    uint32_t header_size = (uint32_t)sizeof(halide_trace_packet_t);
    if (!Packet::read(this, header_size, fdesc)) {
        return false;
    }
    uint32_t payload_size = size - header_size;
    if (payload_size > (uint32_t)sizeof(payload)) {
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

bool Packet::read(void *d, ssize_t size, int file_desc) {
    uint8_t *dst = (uint8_t *)d;
    if (!size) return true;
    for (;;) {
        ssize_t s = ::read(file_desc, dst, size);
        if (s == 0) {
            // EOF
            return false;
        } else if (s < 0) {
            perror("Failed during read");
            exit(-1);
            return 0;
        } else if (s == size) {
            return true;
        }
        size -= s;
        dst += s;
    }
}

void bad_type_error(halide_type_t type) {
    fprintf(stderr, "Can't convert packet with type: %d bits: %d\n", type.code, type.bits);
}

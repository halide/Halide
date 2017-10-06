#ifndef HALIDE_TRACE_UTILS_H
#define HALIDE_TRACE_UTILS_H

#include "HalideRuntime.h"
#include <stdio.h>

namespace Halide {
namespace Internal {

void bad_type_error(halide_type_t type);

// Simple conversion.
template<typename T>
T value_as(halide_type_t type, const void* value) {
    switch (type.code) {
    case halide_type_int:
        switch (type.bits) {
        case 8:
            return (T)(((const int8_t *)value)[0]);
        case 16:
            return (T)(((const int16_t *)value)[0]);
        case 32:
            return (T)(((const int32_t *)value)[0]);
        case 64:
            return (T)(((const int64_t *)value)[0]);
        default:
            bad_type_error(type);
        }
        break;
    case halide_type_uint:
        switch (type.bits) {
        case 8:
            return (T)(((const uint8_t *)value)[0]);
        case 16:
            return (T)(((const uint16_t *)value)[0]);
        case 32:
            return (T)(((const uint32_t *)value)[0]);
        case 64:
            return (T)(((const uint64_t *)value)[0]);
        default:
            bad_type_error(type);
        }
        break;
    case halide_type_float:
        switch (type.bits) {
        case 32:
            return (T)(((const float *)value)[0]);
        case 64:
            return (T)(((const double *)value)[0]);
        default:
            bad_type_error(type);
        }
        break;
    default:
        bad_type_error(type);
    }
    return (T)0;
}

// A struct representing a single Halide tracing packet.
struct Packet : public halide_trace_packet_t {
    // Not all of this will be used, but this
    // is the max possible packet size we
    // consider here.
    uint8_t payload[4096];

    int get_coord(int idx) const {
        return coordinates()[idx];
    }

    template<typename T>
    T get_value_as(int idx) const {
        const uint8_t * val = &(((const uint8_t*)value())[idx*type.bytes()]);
        return value_as<T>(type, (const void*) val);
    }

    // Grab a packet from stdin. Returns false when stdin closes.
    bool read_from_stdin();

    // Grab a packet from a particular fctl file descriptor. Returns false when end is reached.
    bool read_from_filedesc(FILE *fdesc);

private:
    // Do a blocking read of some number of bytes from a unistd file descriptor.
    bool read(void *d, ssize_t size, FILE *fdesc);
};

}
}

#endif

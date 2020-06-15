#ifndef HALIDE_TRACE_UTILS_H
#define HALIDE_TRACE_UTILS_H

#include "HalideRuntime.h"
#include <cstring>
#include <stdio.h>

namespace Halide {
namespace Internal {

void bad_type_error(halide_type_t type);

// Simple conversion. Note: assume that 'value' is aligned properly.
template<typename T>
T value_as(halide_type_t type, const halide_scalar_value_t &value) {
    switch (type.code) {
    case halide_type_int:
        switch (type.bits) {
        case 8:
            return (T)value.u.i8;
        case 16:
            return (T)value.u.i16;
        case 32:
            return (T)value.u.i32;
        case 64:
            return (T)value.u.i64;
        default:
            bad_type_error(type);
        }
        break;
    case halide_type_uint:
        switch (type.bits) {
        case 1:
            return (T)value.u.b;
        case 8:
            return (T)value.u.u8;
        case 16:
            return (T)value.u.u16;
        case 32:
            return (T)value.u.u32;
        case 64:
            return (T)value.u.u64;
        default:
            bad_type_error(type);
        }
        break;
    case halide_type_float:
        switch (type.bits) {
        case 32:
            return (T)value.u.f32;
        case 64:
            return (T)value.u.f64;
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
        const uint8_t *val = (const uint8_t *)(value()) + idx * type.bytes();
        // 'val' may not be aligned: memcpy it to an aligned local
        // so that value_as<>() won't complain under sanitizers.
        halide_scalar_value_t aligned_value;
        // Only copy the number of bytes in the type: the stream isn't guaranteed
        // to be padded to sizeof(halide_scalar_value_t).
        memcpy(&aligned_value, val, type.bits / 8);
        return value_as<T>(type, aligned_value);
    }

    // Grab a packet from stdin. Returns false when stdin closes.
    bool read_from_stdin();

    // Grab a packet from a particular fctl file descriptor. Returns false when end is reached.
    bool read_from_filedesc(FILE *fdesc);

private:
    // Do a blocking read of some number of bytes from a unistd file descriptor.
    bool read(void *d, size_t size, FILE *fdesc);
};

}  // namespace Internal
}  // namespace Halide

#endif

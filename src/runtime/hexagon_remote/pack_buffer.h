#ifndef HALIDE_HEXAGON_REMOTE_BUFFER_PACKING_H
#define HALIDE_HEXAGON_REMOTE_BUFFER_PACKING_H

#include <stdint.h>

template <typename T>
void write_scalar(unsigned char *&write, const T& value) {
    *(T *)write = value;
    write += sizeof(T);
}

template <typename T>
T read_scalar(const unsigned char *&read) {
    T value = *(T *)read;
    read += sizeof(T);
    return value;
}

inline uint32_t packed_buffer_size(uint32_t size, uint32_t alignment) {
    // We write the start and end offsets.
    size += sizeof(uint32_t) * 2;
    // We also need room for the padding.
    size += alignment * 2;

    return size;
}

// Given a pointer, compute an offset to add that is at least offset
// to ensure that start + aligned_offset(start, offset, alignment) is
// aligned to alignment.
inline uint32_t aligned_offset(const unsigned char *start, uint32_t offset, uint32_t alignment) {
    uintptr_t x = (uintptr_t)start;
    x = (x + offset + alignment - 1) & ~(alignment - 1);
    return x - (uintptr_t)start;
}

inline void write_buffer(unsigned char *&write, const unsigned char *buffer, uint32_t size, uint32_t alignment) {
    unsigned char *start = write;
    uint32_t header_size = sizeof(uint32_t) * 2;
    uint32_t offset_start = aligned_offset(start, header_size, alignment);
    uint32_t offset_end = aligned_offset(start, offset_start + size, alignment);
    write_scalar(write, offset_start);
    write_scalar(write, offset_end);
    memcpy(start + offset_start, buffer, size);
    write = start + offset_end;
}

inline const unsigned char *read_buffer(const unsigned char *&read) {
    const unsigned char *start = read;
    uint32_t offset_start = read_scalar<uint32_t>(read);
    uint32_t offset_end = read_scalar<uint32_t>(read);
    read = start + offset_end;
    return start + offset_start;
}

#endif

#ifndef _H_SimpleAppAPI
#define _H_SimpleAppAPI

#include <random>
#include <vector>

#include "HalideRuntime.h"

extern "C" {

// Note that in addition to the calls in this file, the standard Halide runtime
// calls halide_error() and halide_print() are captured in a platform
// specific manner by the standard Simple App code.

// This function outputs the buffer in text form in a platform specific manner.
int halide_buffer_print(const buffer_t* buffer);

// This function outputs the buffer as an image in a platform specific manner.
// For example, in a web based application the buffer contents might be
// displayed as a png image.
int halide_buffer_display(const buffer_t* buffer);

}  // extern "C"

// Fill the buffer's host storage field with random data of the given type
// in the given range. Not very rigorous, but convenient for simple testing
// or profiling.
// If the buffer's elem_size doesn't match the size, or if host is null,
// return false.
template<typename T>
bool halide_randomize_buffer_host(int seed, T min, T max, buffer_t* buf) {
    if (sizeof(T) != buf->elem_size) {
        return false;
    }
    if (!buf->host) {
      return false;
    }
    std::mt19937 rnd(seed);
    T *p0 = reinterpret_cast<T*>(buf->host);
    for (int i0 = 0; i0 < std::max(1, buf->extent[0]); ++i0) {
        T *p1 = p0;
        for (int i1 = 0; i1 < std::max(1, buf->extent[1]); ++i1) {
            T *p2 = p1;
            for (int i2 = 0; i2 < std::max(1, buf->extent[2]); ++i2) {
                T *p3 = p2;
                for (int i3 = 0; i3 < std::max(1, buf->extent[3]); ++i3) {
                    *p3 = min + (T) (((double) rnd() / (double) 0xffffffff) * (max - min));
                    p3 += buf->stride[3];
                }
                p2 += buf->stride[2];
            }
            p1 += buf->stride[1];
        }
        p0 += buf->stride[0];
    }
    return true;
}

#endif  // _H_SimpleAppAPI

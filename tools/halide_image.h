// This header defines a simple Image class which wraps a buffer_t. This is
// useful when interacting with a statically-compiled Halide pipeline emitted by
// Func::compile_to_file, when you do not want to link your processing program
// against Halide.h/libHalide.a.

#ifndef HALIDE_TOOLS_IMAGE_H
#define HALIDE_TOOLS_IMAGE_H

#include <cassert>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>
#include <stdint.h>  // <cstdint> requires C++11

#include "HalideRuntime.h"

namespace Halide {
namespace Tools {

template<typename T>
class Image {
    struct Contents {
        Contents(const buffer_t &b, uint8_t *a) : buf(b), ref_count(1), alloc(a) {}
        buffer_t buf;
        int ref_count;
        uint8_t *alloc;

        void dev_free() {
            halide_device_free(NULL, &buf);
        }

        ~Contents() {
            if (buf.dev) {
                dev_free();
            }
            delete[] alloc;
        }
    };

    Contents *contents;

    void initialize(int x, int y, int z, int w, bool interleaved) {
        buffer_t buf = {0};
        buf.extent[0] = x;
        buf.extent[1] = y;
        buf.extent[2] = z;
        buf.extent[3] = w;
        if (interleaved) {
            buf.stride[0] = z;
            buf.stride[1] = x*z;
            buf.stride[2] = 1;
            buf.stride[3] = x*y*z;
        } else {
            buf.stride[0] = 1;
            buf.stride[1] = x;
            buf.stride[2] = x*y;
            buf.stride[3] = x*y*z;
        }
        buf.elem_size = sizeof(T);

        size_t size = 1;
        if (x) size *= x;
        if (y) size *= y;
        if (z) size *= z;
        if (w) size *= w;

        // Conservatively align images to 128 bytes. This is enough
        // alignment for all the platforms we might use.
        const size_t alignment = 128;
        size = (size + alignment - 1) & ~(alignment - 1);
        uint8_t *ptr = new uint8_t[sizeof(T)*size + alignment - 1];
        buf.host = (uint8_t *)((uintptr_t)(ptr + alignment - 1) & ~(alignment - 1));
        buf.host_dirty = false;
        buf.dev_dirty = false;
        buf.dev = 0;
        contents = new Contents(buf, ptr);
    }

    // Returns the dimension sizes of a statically sized array from inner to outer.
    // E.g. ary[2][3][4] returns (4, 3, 2).
    // Because of the scalar overload below, this will only work if Array is T or T const of
    // one or more dimensions.
    template<typename Array, size_t N>
    static std::vector<int> dimension_sizes(Array (&vals)[N], std::vector<int> dimSizes = std::vector<int>()) {
        dimSizes = dimension_sizes(vals[0], dimSizes);
        dimSizes.push_back((int)N);
        return dimSizes;
    }

    static std::vector<int> dimension_sizes(T const &, std::vector<int> dimSizes = std::vector<int>() ) {
        return dimSizes;
    }

    template<typename Array, size_t N>
    T const * first_of_array(Array (&vals)[N]) {
        return first_of_array(vals[0]);
    }

    T const * first_of_array(T const &val) {
        return &val;
    }

public:
    typedef T ElemType;

    Image() : contents(NULL) {
    }

    Image(int x, int y = 0, int z = 0, int w = 0, bool interleaved = false) {
        initialize(x, y, z, w, interleaved);
    }

    Image(const Image &other) : contents(other.contents) {
        if (contents) {
            contents->ref_count++;
        }
    }

    ~Image() {
        if (contents) {
            contents->ref_count--;
            if (contents->ref_count == 0) {
                delete contents;
                contents = NULL;
            }
        }
    }

    Image &operator=(const Image &other) {
        Contents *p = other.contents;
        if (p) {
            p->ref_count++;
        }
        if (contents) {
            contents->ref_count--;
            if (contents->ref_count == 0) {
                delete contents;
                contents = NULL;
            }
        }
        contents = p;
        return *this;
    }

    T *data() { return (T*)contents->buf.host; }

    const T *data() const { return (T*)contents->buf.host; }

    void set_host_dirty(bool dirty = true) {
        // If you use data directly, you must also call this so that
        // gpu-side code knows that it needs to copy stuff over.
        contents->buf.host_dirty = dirty;
    }

    void copy_to_host() {
        if (contents->buf.dev_dirty) {
            halide_copy_to_host(NULL, &contents->buf);
            contents->buf.dev_dirty = false;
        }
    }

    void copy_to_device(const struct halide_device_interface *device_interface) {
        if (contents->buf.host_dirty) {
            // If host
            halide_copy_to_device(NULL, &contents->buf, device_interface);
            contents->buf.host_dirty = false;
        }
    }

    void dev_free() {
        assert(!contents->buf.dev_dirty);
        contents->dev_free();
    }

    // Initialize the Image from a statically sized array.
    // The data will be copied to internal storage.
    // Because of the scalar overload of dimension_sizes, this will only work if vals
    // is T or T const of one or more dimensions.
    template<typename Array, size_t N>
    explicit Image(Array const (&vals)[N]) {
        std::vector<int> dimSizes(dimension_sizes(vals));
        size_t dims = dimSizes.size();
        assert(dims <= 4);
        initialize
            ( dims > 0 ? dimSizes[0] : 1
            , dims > 1 ? dimSizes[1] : 0
            , dims > 2 ? dimSizes[2] : 0
            , dims > 3 ? dimSizes[3] : 0
            , false);
        int n = 1;
        for (size_t i = 0; i < dims; ++i)
            n *= dimSizes[i];
        T const *ary = first_of_array(vals);
        T *host = data();
        for (int i = 0; i < n; ++i)
            host[i] = ary[i];
    }

    /** Make sure you've called copy_to_host before you start
     * accessing pixels directly. */
    T &operator()(int x, int y = 0, int z = 0, int w = 0) {
        T *ptr = (T *)contents->buf.host;
        x -= contents->buf.min[0];
        y -= contents->buf.min[1];
        z -= contents->buf.min[2];
        w -= contents->buf.min[3];
        size_t s0 = contents->buf.stride[0];
        size_t s1 = contents->buf.stride[1];
        size_t s2 = contents->buf.stride[2];
        size_t s3 = contents->buf.stride[3];
        return ptr[s0 * x + s1 * y + s2 * z + s3 * w];
    }

    /** Make sure you've called copy_to_host before you start
     * accessing pixels directly */
    const T &operator()(int x, int y = 0, int z = 0, int w = 0) const {
        const T *ptr = (const T *)contents->buf.host;
        x -= contents->buf.min[0];
        y -= contents->buf.min[1];
        z -= contents->buf.min[2];
        w -= contents->buf.min[3];
        size_t s0 = contents->buf.stride[0];
        size_t s1 = contents->buf.stride[1];
        size_t s2 = contents->buf.stride[2];
        size_t s3 = contents->buf.stride[3];
        return ptr[s0 * x + s1 * y + s2 * z + s3 * w];
    }

    operator buffer_t *() const {
        return &(contents->buf);
    }

    int width() const {
        return dimensions() > 0 ? contents->buf.extent[0] : 1;
    }

    int height() const {
        return dimensions() > 1 ? contents->buf.extent[1] : 1;
    }

    int channels() const {
        return dimensions() > 2 ? contents->buf.extent[2] : 1;
    }

    int dimensions() const {
        for (int i = 0; i < 4; i++) {
            if (contents->buf.extent[i] == 0) {
                return i;
            }
        }
        return 4;
    }

    int stride(int dim) const {
        return contents->buf.stride[dim];
    }

    int min(int dim) const {
        return contents->buf.min[dim];
    }

    int extent(int dim) const {
        return contents->buf.extent[dim];
    }

    void set_min(int x, int y = 0, int z = 0, int w = 0) {
        contents->buf.min[0] = x;
        contents->buf.min[1] = y;
        contents->buf.min[2] = z;
        contents->buf.min[3] = w;
    }
};

}  // namespace Tools
}  // namespace Halide

#endif  // HALIDE_TOOLS_IMAGE_H

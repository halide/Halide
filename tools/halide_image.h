// This header defines a simple Image class which wraps a halide_buffer_t. This is
// useful when interacting with a statically-compiled Halide pipeline emitted by
// Func::compile_to_file, when you do not want to link your processing program
// against Halide.h/libHalide.a.

#ifndef HALIDE_TOOLS_IMAGE_H
#define HALIDE_TOOLS_IMAGE_H

#include <cassert>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdint.h>  // <cstdint> requires C++11
#include <string.h>

#include "HalideRuntime.h"

namespace Halide {
namespace Tools {

template<typename T>
class Image {
    struct Contents {
        halide_buffer_t buf;
        // This class supports up to four dimensions
        halide_dimension_t shape[4];
        int ref_count;
        uint8_t *alloc = NULL;

        void device_free() {
            halide_device_free(NULL, &buf);
        }

        Contents() : ref_count(1) {
            memset(&buf, 0, sizeof(buf));
            memset(&shape, 0, sizeof(shape));
        }

        ~Contents() {
            if (buf.device) {
                device_free();
            }
            delete[] alloc;
        }
    };

    Contents *contents;

    void initialize(int x, int y, int z, int w, bool interleaved) {
        contents = new Contents;

        contents->shape[0].extent = x;
        contents->shape[1].extent = y;
        contents->shape[2].extent = z;
        contents->shape[3].extent = w;
        if (interleaved) {
            contents->shape[0].stride = z;
            contents->shape[1].stride = x*z;
            contents->shape[2].stride = 1;
            contents->shape[3].stride = x*y*z;
        } else {
            contents->shape[0].stride = 1;
            contents->shape[1].stride = x;
            contents->shape[2].stride = x*y;
            contents->shape[3].stride = x*y*z;
        }
        contents->buf.type = halide_type_of<T>();
        contents->buf.dimensions = w ? 4 : z ? 3 : y ? 2 : x ? 1 : 0;
        contents->buf.dim = contents->shape;

        size_t size = 1;
        if (x) size *= x;
        if (y) size *= y;
        if (z) size *= z;
        if (w) size *= w;

        uint8_t *ptr = new uint8_t[sizeof(T)*size + 40];
        contents->alloc = ptr;
        while ((size_t)ptr & 0x1f) ptr++;
        contents->buf.host = ptr;
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
        contents->buf.set_host_dirty(dirty);
    }

    void copy_to_host() {
        if (contents->buf.device_dirty()) {
            halide_copy_to_host(NULL, &contents->buf);
        }
    }

    void copy_to_device(const struct halide_device_interface_t *device_interface) {
        if (contents->buf.host_dirty()) {
            halide_copy_to_device(NULL, &contents->buf, device_interface);
        }
    }

    void device_free() {
        assert(!contents->buf.device_dirty());
        contents->device_free();
    }

    Image(T vals[]) {
        initialize(sizeof(vals)/sizeof(T));
        for (int i = 0; i < sizeof(vals); i++) (*this)(i) = vals[i];
    }

    /** Make sure you've called copy_to_host before you start
     * accessing pixels directly. */
    T &operator()(int x, int y = 0, int z = 0, int w = 0) {
        T *ptr = (T *)contents->buf.host;
        x -= contents->shape[0].min;
        y -= contents->shape[1].min;
        z -= contents->shape[2].min;
        w -= contents->shape[3].min;
        size_t s0 = contents->shape[0].stride;
        size_t s1 = contents->shape[1].stride;
        size_t s2 = contents->shape[2].stride;
        size_t s3 = contents->shape[3].stride;
        return ptr[s0 * x + s1 * y + s2 * z + s3 * w];
    }

    /** Make sure you've called copy_to_host before you start
     * accessing pixels directly */
    const T &operator()(int x, int y = 0, int z = 0, int w = 0) const {
        const T *ptr = (const T *)contents->buf.host;
        x -= contents->shape[0].min;
        y -= contents->shape[1].min;
        z -= contents->shape[2].min;
        w -= contents->shape[3].min;
        size_t s0 = contents->shape[0].stride;
        size_t s1 = contents->shape[1].stride;
        size_t s2 = contents->shape[2].stride;
        size_t s3 = contents->shape[3].stride;
        return ptr[s0 * x + s1 * y + s2 * z + s3 * w];
    }

    operator halide_buffer_t *() const {
        return &(contents->buf);
    }

    halide_buffer_t *raw_buffer() {
        return &(contents->buf);
    }

    const halide_buffer_t *raw_buffer() const {
        return &(contents->buf);
    }

    int width() const {
        return (dimensions() > 0) ? dim(0).extent : 1;
    }

    int height() const {
        return (dimensions() > 1) ? dim(1).extent : 1;
    }

    int channels() const {
        return (dimensions() > 2) ? dim(2).extent : 1;
    }

    int dimensions() const {
        return contents->buf.dimensions;
    }

    const halide_dimension_t &dim(int d) const {
        return contents->shape[d];
    }

    halide_dimension_t &dim(int d) {
        return contents->shape[d];
    }

    void set_min(int x, int y = 0, int z = 0, int w = 0) {
        contents->shape[0].min = x;
        contents->shape[1].min = y;
        contents->shape[2].min = z;
        contents->shape[3].min = w;
    }
};

}  // namespace Tools
}  // namespace Halide

#include "halide_image_info.h"
#endif  // HALIDE_TOOLS_IMAGE_H

// This header defines a simple Image class which wraps a buffer_t. This is
// useful when interacting with a statically-compiled Halide pipeline emitted by
// Func::compile_to_file, when you do not want to link your processing program
// against Halide.h/libHalide.a.

#ifndef _STATIC_IMAGE_H
#define _STATIC_IMAGE_H

#include <stdint.h>
#include <memory>
#include <limits>
#include <stdlib.h>
#include <cassert>

#ifndef BUFFER_T_DEFINED
#define BUFFER_T_DEFINED
#include <stdint.h>
typedef struct buffer_t {
    uint64_t dev;
    uint8_t* host;
    int32_t extent[4];
    int32_t stride[4];
    int32_t min[4];
    int32_t elem_size;
    bool host_dirty;
    bool dev_dirty;
} buffer_t;
#endif

extern "C" int halide_copy_to_host(void *user_context, buffer_t* buf);
extern "C" int halide_dev_free(void *user_context, buffer_t *buf);

template<typename T>
class Image {
    struct Contents {
        Contents(buffer_t b, uint8_t* a) : buf(b), ref_count(1), alloc(a) {}
        buffer_t buf;
        int ref_count;
        uint8_t *alloc;

        void dev_free() {
            halide_dev_free(NULL, &buf);
        }

        ~Contents() {
            if (buf.dev) {
                dev_free();
            }
            delete[] alloc;
        }
    };

    Contents *contents;

    void initialize(int x, int y, int z, int w) {
        buffer_t buf = {0};
        buf.extent[0] = x;
        buf.extent[1] = y;
        buf.extent[2] = z;
        buf.extent[3] = w;
        buf.stride[0] = 1;
        buf.stride[1] = x;
        buf.stride[2] = x*y;
        buf.stride[3] = x*y*z;
        buf.elem_size = sizeof(T);

        size_t size = 1;
        if (x) size *= x;
        if (y) size *= y;
        if (z) size *= z;
        if (w) size *= w;

        uint8_t *ptr = new uint8_t[sizeof(T)*size + 40];
        buf.host = ptr;
        buf.host_dirty = false;
        buf.dev_dirty = false;
        buf.dev = 0;
        while ((size_t)buf.host & 0x1f) buf.host++;
        contents = new Contents(buf, ptr);
    }

public:
    Image() : contents(NULL) {
    }

    Image(int x, int y = 0, int z = 0, int w = 0) {
        initialize(x, y, z, w);
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

    T *data() const {return (T*)contents->buf.host;}

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

    void copy_to_dev() {
        if (contents->buf.host_dirty) {
            halide_copy_to_dev(NULL, &contents->buf);
            contents->buf.host_dirty = false;
        }
    }

    void dev_free() {
        assert(!contents->buf.dev_dirty);
        contents->dev_free();
    }

    Image(T vals[]) {
        initialize(sizeof(vals)/sizeof(T));
        for (int i = 0; i < sizeof(vals); i++) (*this)(i) = vals[i];
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

#endif //_STATIC_IMAGE_H

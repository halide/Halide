// This header defines a simple Image class which wraps a buffer_t. This is
// useful when interacting with a statically-compiled Halide pipeline emitted by
// Func::compileToFile, when you do not want to link your processing program
// against Halide.h/libHalide.a.

#ifndef _STATIC_IMAGE_H
#define _STATIC_IMAGE_H

#include <stdint.h>
#include <memory>
#include <limits>

#include <tr1/memory>
using std::tr1::shared_ptr;

#ifndef BUFFER_T_DEFINED
#define BUFFER_T_DEFINED
#include <stdint.h>
typedef struct buffer_t {
  uint8_t* host;
  uint64_t dev;
  bool host_dirty;
  bool dev_dirty;
  int32_t extent[4];
  int32_t stride[4];
  int32_t min[4];
  size_t elem_size;
} buffer_t;
#endif

extern "C" void halide_copy_to_host(buffer_t* buf);

template<typename T>
class Image {
    struct Contents {
        Contents(buffer_t b, uint8_t* a) {buf = b; alloc = a;}
        buffer_t buf;
        uint8_t *alloc;
        ~Contents() {
            delete[] alloc;
        }        
    };

    shared_ptr<Contents> contents;

    void initialize(int w, int h, int c) {
        buffer_t buf;
        buf.extent[0] = w;
        buf.extent[1] = h;
        buf.extent[2] = c;
        buf.extent[3] = 1;
        buf.stride[0] = 1;
        buf.stride[1] = w;
        buf.stride[2] = w*h;
        buf.stride[3] = 0;
        buf.min[0] = 0;
        buf.min[1] = 0;
        buf.min[2] = 0;
        buf.min[3] = 0;
        buf.elem_size = sizeof(T);

        uint8_t *ptr = new uint8_t[sizeof(T)*w*h*c+32];
        buf.host = ptr;
        buf.host_dirty = false;
        buf.dev_dirty = false;
        buf.dev = 0;
        while ((size_t)buf.host & 0x1f) buf.host++; 
        contents.reset(new Contents(buf, ptr));
    }

public:
    Image() {
    }
    
    Image(int w, int h = 1, int c = 1) {
        initialize(w, h, c);
    }

    T *data() {return (T*)contents->buf.host;}

    void markHostDirty() {
        // If you use data directly, you must also call this so that
        // gpu-side code knows that it needs to copy stuff over.
        contents->buf.host_dirty = true;
    }

    void copyToHost() {
        if (contents->buf.dev_dirty) {
            halide_copy_to_host(&contents->buf);
            contents->buf.dev_dirty = false;
        }
    }

    Image(T vals[]) {
        initialize(sizeof(vals)/sizeof(T), 1, 1);
        for (int i = 0; i < sizeof(vals); i++) (*this)(i, 0, 0) = vals[i];
    }

    void copy(T* vals, int width, int height) {
        assert(contents && "Copying into an uninitialized image");
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < height; x++) {
                (*this)(x, y, 0) = vals[y * width + x];
            }
        }
    }

    // Warning. This is slower than you might expect
    T &operator()(int x, int y = 0, int c = 0) {
        copyToHost();
        markHostDirty();

        T *ptr = (T *)contents->buf.host;
        int w = contents->buf.extent[0];
        int h = contents->buf.extent[1];
        return ptr[(c*h + y)*w + x];
    }

    const T &operator()(int x, int y = 0, int c = 0) const {
        copyToHost();

        const T *ptr = (const T *)contents->buf.host;
        return ptr[c*contents->buf.stride[2] + y*contents->buf.stride[1] + x*contents->buf.stride[0]];
    }

    operator buffer_t *() {
        return &(contents->buf);
    }

    int width() {
        return contents->buf.extent[0];
    }

    int height() {
        return contents->buf.extent[1];
    }

    int channels() {
        return contents->buf.extent[2];
    }

    int stride(int dim) {
        return contents->buf.stride[dim];
    }

    int size(int dim) {
        return contents->buf.extent[dim];
    }

};

#endif //_STATIC_IMAGE_H

#ifndef HALIDE_IMAGE_H
#define HALIDE_IMAGE_H

#include "Buffer.h"

namespace Halide {

template<typename T>
class Image {
private:
    const Buffer buffer;    

    // These fields are also stored in the buffer, but they're cached
    // here in the handle to make operator() fast. This is safe to do
    // because the buffer is const.
    T *base;
    int stride_1, stride_2, stride_3;

    void create_buffer(int x, int y, int z, int w) {
        Internal::BufferContents *buf = new Internal::BufferContents;
        buf->buf.host = (uint8_t *)calloc(sizeof(T), x*y*z*w);
        buf->buf.host_dirty = false;
        buf->buf.dev_dirty = false;
        buf->buf.extent[0] = x;
        buf->buf.extent[1] = y;
        buf->buf.extent[2] = z;
        buf->buf.extent[3] = w;
        buf->buf.stride[0] = 1;
        buf->buf.stride[1] = x;
        buf->buf.stride[2] = x*y;
        buf->buf.stride[3] = x*y*z;
        buf->buf.min[0] = 0;
        buf->buf.min[1] = 0;
        buf->buf.min[2] = 0;
        buf->buf.min[3] = 0;
        buf->buf.elem_size = sizeof(T);
        buf->ref_count = 0;
        buf->type = type_of<T>();
        buf->own_host_allocation = true;

        base = buf->buf.host;
        stride_1 = buf->buf.stride[1];
        stride_2 = buf->buf.stride[2];
        stride_3 = buf->buf.stride[3];

        buffer.contents = Internal::IntrusivePtr<Internal::BufferContents>(buf);
    }

public:
    Image() {}

    Image(int x) {
        create_buffer(x, 1, 1, 1);
    }

    Image(int x, int y) {
        create_buffer(x, y, 1, 1);
    }

    Image(int x, int y, int z) {
        create_buffer(x, y, z, 1);
    }

    Image(int x, int y, int z, int w) {
        create_buffer(x, y, z, w);
    }

    bool defined() {return buffer.defined();}

    Image(const Buffer &buf) : buffer(buf) {
        if (buffer.defined()) {
            base = buf.contents.ptr->buf.host;
            stride_1 = buf.contents.ptr->buf.stride[1];
            stride_2 = buf.contents.ptr->buf.stride[2];
            stride_3 = buf.contents.ptr->buf.stride[3];
        }        
    }

    Image(const buffer_t *b) {
        Internal::BufferContents *buf = new Internal::BufferContents;
        buf->buf = *b;
        buf->ref_count = 0;
        buf->type = type_of<T>();
        buf->own_host_allocation = false;
        base = buf->buf.host;
        stride_1 = buf->buf.stride[1];
        stride_2 = buf->buf.stride[2];
        stride_3 = buf->buf.stride[3];
        buffer.contents = Internal::IntrusivePtr<Internal::BufferContents>(buf);        
    }

    T operator()(int x) const {
        return base[x];
    }

    T operator()(int x, int y) const {
        return base[x + y*stride_1];
    }

    T operator()(int x, int y, int z) const {
        return base[x + y*stride_1 + z*stride_2];
    }

    T operator()(int x, int y, int z, int w) const {
        return base[x + y*stride_1 + z*stride_2 + w*stride_3];
    }

    T &operator()(int x) {
        return base[x];
    }

    T &operator()(int x, int y) {
        return base[x + y*stride_1];
    }

    T &operator()(int x, int y, int z) {
        return base[x + y*stride_1 + z*stride_2];
    }

    T &operator()(int x, int y, int z, int w) {
        return base[x + y*stride_1 + z*stride_2 + w*stride_3];
    }

    Expr operator()(Expr x) {
        vector<Expr> args;
        args.push_back(x);
        return new Call(type_of<T>(), name(), args, Call::Image, NULL, buffer);
    }

    Expr operator()(Expr x, Expr y) {
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        return new Call(type_of<T>(), name(), args, Call::Image, NULL, buffer);
    }

    Expr operator()(Expr x, Expr y, Expr z, Expr w) {
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        args.push_back(w);
        return new Call(type_of<T>(), name(), args, Call::Image, NULL, buffer);
    }
    
    operator const buffer_t *() {return buffer.raw_buffer();}
    operator Buffer() {return buffer;}
};

}

#endif

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

    void prepare_for_direct_pixel_access() {
        // TODO: make sure buffer has been copied to host
        if (buffer.defined()) {
            base = (T *)buffer.host_ptr();
            stride_1 = buffer.stride(1);
            stride_2 = buffer.stride(2);
            stride_3 = buffer.stride(3);
        } else {
            base = NULL;
            stride_1 = stride_2 = stride_3 = 0;
        }
    }

public:
    Image() {}

    Image(int x, int y = 1, int z = 1, int w = 1) : buffer(Buffer(type_of<T>(), x, y, z, w)) {
        prepare_for_direct_pixel_access();
    }

    Image(const Buffer &buf) : buffer(buf) {
        prepare_for_direct_pixel_access();
    }

    Image(const buffer_t *b) : buffer(type_of<T>, b) {
        prepare_for_direct_pixel_access();
    }

    bool defined() {
        return buffer.defined();
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
        return new Call(type_of<T>(), buffer.name(), args, Call::Image, Internal::Function(), buffer);
    }

    Expr operator()(Expr x, Expr y) {
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        return new Call(type_of<T>(), buffer.name(), args, Call::Image, Internal::Function(), buffer);
    }

    Expr operator()(Expr x, Expr y, Expr z) {
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        return new Call(type_of<T>(), buffer.name(), args, Call::Image, Internal::Function(), buffer);
    }

    Expr operator()(Expr x, Expr y, Expr z, Expr w) {
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        args.push_back(w);
        return new Call(type_of<T>(), buffer.name(), args, Call::Image, Internal::Function(), buffer);
    }
    
    operator const buffer_t *() {return buffer.raw_buffer();}
    operator Buffer() {return buffer;}
};

}

#endif

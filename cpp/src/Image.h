#ifndef HALIDE_IMAGE_H
#define HALIDE_IMAGE_H

#include "Buffer.h"

namespace Halide {

template<typename T>
class Image {
private:
    Buffer buffer;    

    // These fields are also stored in the buffer, but they're cached
    // here in the handle to make operator() fast. This is safe to do
    // because the buffer is const.
    T *base;
    int stride_1, stride_2, stride_3, dims;

    void prepare_for_direct_pixel_access() {
        // TODO: make sure buffer has been copied to host
        if (buffer.defined()) {
            base = (T *)buffer.host_ptr();
            stride_1 = buffer.stride(1);
            stride_2 = buffer.stride(2);
            stride_3 = buffer.stride(3);
            dims = buffer.dimensions();
        } else {
            base = NULL;
            stride_1 = stride_2 = stride_3 = 0;
            dims = 0;
        }
    }

public:
    Image() {}

    Image(int x, int y = 0, int z = 0, int w = 0) : buffer(Buffer(type_of<T>(), x, y, z, w)) {
        prepare_for_direct_pixel_access();
    }

    Image(const Buffer &buf) : buffer(buf) {
        prepare_for_direct_pixel_access();
    }

    Image(const buffer_t *b) : buffer(type_of<T>, b) {
        prepare_for_direct_pixel_access();
    }

    bool defined() const {
        return buffer.defined();
    }

    int dimensions() const {
        return dims;
    }

    int extent(int dim) const {
        assert(defined());
        assert(dim >= 0 && dim < 4 && "We only support 4-dimensional buffers for now");
        return buffer.extent(dim);
    }

    int width() const {
        return extent(0);
    }

    int height() const {
        return extent(1);
    }

    int channels() const {
        return extent(2);
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

    Expr operator()() const {
        assert(dims >= 0);
        vector<Expr> args;        
        for (int i = 0; args.size() < (size_t)dims; i++) {
            args.push_back(Var::implicit(i));
        }
        return new Call(buffer, args);
    }

    Expr operator()(Expr x) const {
        assert(dims >= 1);
        vector<Expr> args;
        args.push_back(x);
        for (int i = 0; args.size() < (size_t)dims; i++) {
            args.push_back(Var::implicit(i));
        }
        return new Call(buffer, args);
    }

    Expr operator()(Expr x, Expr y) const {
        assert(dims >= 2);
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        for (int i = 0; args.size() < (size_t)dims; i++) {
            args.push_back(Var::implicit(i));
        }
        return new Call(buffer, args);
    }

    Expr operator()(Expr x, Expr y, Expr z) const {
        assert(dims >= 3);
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        for (int i = 0; args.size() < (size_t)dims; i++) {
            args.push_back(Var::implicit(i));
        }
        return new Call(buffer, args);
    }

    Expr operator()(Expr x, Expr y, Expr z, Expr w) const {
        assert(dims >= 4);
        vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        args.push_back(w);
        for (int i = 0; args.size() < (size_t)dims; i++) {
            args.push_back(Var::implicit(i));
        }
        return new Call(buffer, args);
    }
    
    operator const buffer_t *() const {return buffer.raw_buffer();}
    operator Buffer() const {return buffer;}
    operator Expr() const {return (*this)();}
};

}

#endif

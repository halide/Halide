#ifndef HALIDE_IMAGE_H
#define HALIDE_IMAGE_H

#include "Buffer.h"

namespace Halide {

template<typename T>
class Image {
private:
    Buffer buf;    

    // These fields are also stored in the buffer, but they're cached here in the handle to make operator() fast
    T *base;
    int stride_1, stride_2, stride_3;

public:
    Image();
    Image(int);
    Image(int, int);
    Image(int, int, int);
    Image(int, int, int, int);
    Image(const Buffer &);
    Image(const buffer_t *);

    T operator()(int) const;
    T operator()(int, int) const;
    T operator()(int, int, int) const;
    T operator()(int, int, int, int) const;

    T &operator()(int);
    T &operator()(int, int);
    T &operator()(int, int, int);
    T &operator()(int, int, int, int);
    
    operator const buffer_t *() {return buf.raw_buffer();}
    operator Buffer() {return buf;}
};

}

#endif

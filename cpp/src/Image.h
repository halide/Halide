#ifndef HALIDE_IMAGE_H
#define HALIDE_IMAGE_H

/** \file
 * Defines Halide's Image data type 
 */

#include "Buffer.h"

namespace Halide {

/** A reference-counted handle on a dense multidimensional array
 * containing scalar values of type T. Can be directly accessed and
 * modified. May have up to four dimensions. Color images are
 * represented as three-dimensional, with the third dimension being
 * the color channel. In general we store color images in
 * color-planes, as opposed to packed RGB, because this tends to
 * vectorize more cleanly. */
template<typename T>
class Image {
private:
    /** The underlying memory object */
    Buffer buffer;    

    /** These fields are also stored in the buffer, but they're cached
     * here in the handle to make operator() fast. This is safe to do
     * because the buffer is never modified 
     */
    // @{
    T *base;
    int stride_1, stride_2, stride_3, dims;
    // @}

    /** Prepare the buffer to be used as an image. Makes sure that the
     * cached strides are correct, and that the image data is on the
     * host. */
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
    /** Construct an undefined image handle */
    Image() {}

    /** Allocate an image with the given dimensions. */
    Image(int x, int y = 0, int z = 0, int w = 0) : buffer(Buffer(type_of<T>(), x, y, z, w)) {
        prepare_for_direct_pixel_access();
    }

    /** Wrap a buffer in an Image object, so that we can directly
     * access its pixels in a type-safe way. */
    Image(const Buffer &buf) : buffer(buf) {
        prepare_for_direct_pixel_access();
    }

    /** Wrap a buffer_t in an Image object, so that we can access its
     * pixels. */
    Image(const buffer_t *b) : buffer(type_of<T>(), b) {
        prepare_for_direct_pixel_access();
    }

    /** Check if this image handle points to actual data */
    bool defined() const {
        return buffer.defined();
    }

    /** Get the dimensionality of the data. Typically two for grayscale images, and three for color images. */
    int dimensions() const {
        return dims;
    }

    /** Get the size of a dimension */
    int extent(int dim) const {
        assert(defined());
        assert(dim >= 0 && dim < 4 && "We only support 4-dimensional buffers for now");
        return buffer.extent(dim);
    }

    /** Get the number of elements in the buffer between two adjacent
     * elements in the given dimension. For example, the stride in
     * dimension 0 is usually 1, and the stride in dimension 1 is
     * usually the extent of dimension 0. This is not necessarily true
     * though. */
    int stride(int dim) const {
        assert(defined());
        assert(dim >= 0 && dim < 4 && "We only support 4-dimensional buffers for now");
        return buffer.stride(dim);
    }

    /** Get the extent of dimension 0, which by convention we use as
     * the width of the image */
    int width() const {
        return extent(0);
    }

    /** Get the extent of dimension 1, which by convention we use as
     * the height of the image */
    int height() const {
        return extent(1);
    }

    /** Get the extent of dimension 2, which by convention we use as
     * the number of color channels (often 3) */
    int channels() const {
        return extent(2);
    }

    /** Get a pointer to the first element. */
    T *data() const {
        return base;
    }

    /** Assuming this image is one-dimensional, get the value of the
     * element at position x */
    T operator()(int x) const {
        return base[x];
    }

    /** Assuming this image is two-dimensional, get the value of the
     * element at position (x, y) */
    T operator()(int x, int y) const {
        return base[x + y*stride_1];
    }

    /** Assuming this image is three-dimensional, get the value of the
     * element at position (x, y, z) */
    T operator()(int x, int y, int z) const {
        return base[x + y*stride_1 + z*stride_2];
    }

    /** Assuming this image is four-dimensional, get the value of the
     * element at position (x, y, z, w) */
    T operator()(int x, int y, int z, int w) const {
        return base[x + y*stride_1 + z*stride_2 + w*stride_3];
    }

    /** Assuming this image is one-dimensional, get a reference to the
     * element at position x */
    T &operator()(int x) {
        return base[x];
    }

    /** Assuming this image is two-dimensional, get a reference to the
     * element at position (x, y) */
    T &operator()(int x, int y) {
        return base[x + y*stride_1];
    }

    /** Assuming this image is three-dimensional, get a reference to the
     * element at position (x, y, z) */
    T &operator()(int x, int y, int z) {
        return base[x + y*stride_1 + z*stride_2];
    }

    /** Assuming this image is four-dimensional, get a reference to the
     * element at position (x, y, z, w) */
    T &operator()(int x, int y, int z, int w) {
        return base[x + y*stride_1 + z*stride_2 + w*stride_3];
    }

    /** Construct an expression which loads from this image. The
     * location is composed of enough implicit variables to match the
     * dimensionality of the image (see \ref Var::implicit) */
    Expr operator()() const {
        assert(dims >= 0);
        std::vector<Expr> args;        
        for (int i = 0; args.size() < (size_t)dims; i++) {
            args.push_back(Var::implicit(i));
        }
        return new Internal::Call(buffer, args);
    }    

    /** Construct an expression which loads from this image. The
     * location is extended with enough implicit variables to match
     * the dimensionality of the image (see \ref Var::implicit) */
    // @{
    Expr operator()(Expr x) const {
        assert(dims >= 1);
        std::vector<Expr> args;
        args.push_back(x);
        for (int i = 0; args.size() < (size_t)dims; i++) {
            args.push_back(Var::implicit(i));
        }
        return new Internal::Call(buffer, args);
    }

    Expr operator()(Expr x, Expr y) const {
        assert(dims >= 2);
        std::vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        for (int i = 0; args.size() < (size_t)dims; i++) {
            args.push_back(Var::implicit(i));
        }
        return new Internal::Call(buffer, args);
    }

    Expr operator()(Expr x, Expr y, Expr z) const {
        assert(dims >= 3);
        std::vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        for (int i = 0; args.size() < (size_t)dims; i++) {
            args.push_back(Var::implicit(i));
        }
        return new Internal::Call(buffer, args);
    }

    Expr operator()(Expr x, Expr y, Expr z, Expr w) const {
        assert(dims >= 4);
        std::vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        args.push_back(w);
        for (int i = 0; args.size() < (size_t)dims; i++) {
            args.push_back(Var::implicit(i));
        }
        return new Internal::Call(buffer, args);
    }
    // @}

    /** Get a pointer to the raw buffer_t that this image holds */
    operator const buffer_t *() const {return buffer.raw_buffer();}

    /** Get a handle on the Buffer that this image holds */
    operator Buffer() const {return buffer;}

    /** Treating the image as an Expr is equivalent to call it with no
     * arguments. For example, you can say:
     *
     \code
     Image im(10, 10);
     Func f;
     f = im*2;
     \endcode
     * 
     * This will define f as a two-dimensional function with value at
     * position (x, y) equal to twice the value of the image at the
     * same location.
     */
    operator Expr() const {return (*this)();}


};

}

#endif

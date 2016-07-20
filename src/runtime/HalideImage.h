// A simple Image class which wraps a halide_buffer_t. This is useful
// when interacting with a statically-compiled Halide pipeline emitted
// by Func::compile_to_file, when you do not want to link your
// processing program against Halide.h/libHalide.a.

#ifndef HALIDE_RUNTIME_IMAGE_H
#define HALIDE_RUNTIME_IMAGE_H

#include <memory>
#include <vector>
#include <cassert>
#include <stdint.h>
#include <string.h>

#include "HalideRuntime.h"

namespace Halide {
namespace Tools {

/** A templated Image class derived from halide_buffer_t that adds
 * functionality. T is the element type, and D is the maximum number
 * of dimensions. It can usually be left at the default of 4. */
template<typename T, int D = 4>
class Image : public halide_buffer_t {
    halide_dimension_t shape[D];
    std::shared_ptr<uint8_t> alloc;

    void initialize_from_buffer(const halide_buffer_t *buf) {
        assert(buf->dimensions <= D);
        memcpy((halide_buffer_t *)this, buf, sizeof(halide_buffer_t));
        memcpy(shape, buf->dim, buf->dimensions * sizeof(halide_dimension_t));
        dim = &shape[0];
    }

    // Initialize the shape from a parameter pack of ints
    template<typename ...Args>
    void initialize_shape(halide_dimension_t *next, int first, Args... rest) {
        next->min = 0;
        next->extent = first;
        if (next == shape) {
            next->stride = 1;
        } else {
            next->stride = (next-1)->stride * (next-1)->extent;
        }
        initialize_shape(next + 1, rest...);
    }

    void initialize_shape(halide_dimension_t *) {
    }

    // Initialize the shape from the static shape of an array
    template<typename Array, size_t N>
    void initialize_shape_from_array_shape(halide_dimension_t *next, Array (&vals)[N]) {
        next->min = 0;
        next->extent = (int)N;
        if (next == shape) {
            next->stride = 1;
        } else {
            halide_dimension_t *prev = next-1;
            initialize_shape_from_array_shape(prev, vals[0]);
            next->stride = prev->stride * prev->extent;
        }
    }

    void initialize_shape_from_array_shape(halide_dimension_t *, T &) {
    }

    template<typename Array, size_t N>
    int dimensionality_of_array(Array (&vals)[N]) {
        return dimensionality_of_array(vals[0]) + 1;
    }

    int dimensionality_of_array(T &) {
        return 0;
    }

    template<typename ...Args>
    bool any_zero(int first, Args... rest) {
        if (first == 0) return true;
        return any_zero(rest...);
    }

    bool any_zero() {
        return false;
    }

public:
    typedef T ElemType;

    // We need custom constructors and assignment operators so that we
    // can ensure the dim pointer always points to this Image's shape
    // array.
    Image() {
        memset((halide_buffer_t *)this, 0, sizeof(halide_buffer_t));
        memset(shape, 0, sizeof(shape));
    }

    Image(const Image<T, D> &other) : alloc(other.alloc) {
        initialize_from_buffer(&other);
    }

    Image(const Image<T, D> &&other) : alloc(std::move(other.alloc)) {
        initialize_from_buffer(&other);
    }

    Image<T, D> &operator=(const Image<T, D> &other) {
        initialize_from_buffer(&other);
        alloc = other.alloc;
        return *this;
    }

    Image<T, D> &operator=(const Image<T, D> &&other) {
        initialize_from_buffer(&other);
        alloc = std::move(other.alloc);
        return *this;
    }

    Image(const halide_buffer_t &buf) {
        initialize_from_buffer(&buf);
    }

    /** Allocate memory for this Image. Drops the reference to any
     * existing memory. Call this after doing a bounds query. */
    void allocate() {
        // Conservatively align images to 128 bytes. This is enough
        // alignment for all the platforms we might use.
        size_t size = size_in_bytes();
        const size_t alignment = 128;
        size = (size + alignment - 1) & ~(alignment - 1);
        uint8_t *ptr = (uint8_t *)malloc(sizeof(T)*size + alignment - 1);
        host = (uint8_t *)((uintptr_t)(ptr + alignment - 1) & ~(alignment - 1));
        alloc.reset(ptr, free);
    }

    /** Allocate a new image of the given size. Use zeroes to make a
     * bounds query buffer. */
    template<typename ...Args>
    Image(int first, Args... rest) {
        static_assert(sizeof...(rest) < D,
                      "Too many arguments to constructor. Use Image<T, D>, where D is at least the desired number of dimensions");
        initialize_shape(shape, first, rest...);
        type = halide_type_of<T>();
        dimensions = 1 + (int)(sizeof...(rest));
        dim = &shape[0];
        device = 0;
        device_interface = nullptr;
        flags = 0;
        host = nullptr;
        if (!any_zero(first, rest...)) {
            allocate();
        }
    }

    /** Make an image that refers to a statically sized array. Does not
     * take ownership of the data. */
    template<typename Array, size_t N>
    explicit Image(Array (&vals)[N]) {
        dimensions = dimensionality_of_array(vals);
        initialize_shape_from_array_shape(shape + dimensions - 1, vals);
        type = halide_type_of<T>();
        dim = &shape[0];
        device = 0;
        device_interface = nullptr;
        flags = 0;
        host = (uint8_t *)vals;
    }

    /** Initialize an Image from a pointer and some sizes. Assumes
     * dense row-major packing. Does not take ownership of the data. */
    template<typename ...Args>
    explicit Image(T *data, int first, Args... rest) {
        static_assert(sizeof...(rest) < D,
                      "Too many arguments to constructor. Use Image<T, D>, where D is at least the desired number of dimensions");
        initialize_shape(0, first, rest...);
        type = halide_type_of<T>();
        dimensions = 1 + (int)(sizeof...(rest));
        dim = &shape[0];
        host = data;
        device = 0;
        device_interface = nullptr;
        flags = 0;
    }

    /** If you use the (x, y, c) indexing convention, then Halide
     * Images are stored planar by default. This function constructs
     * an interleaved RGB or RGBA image that can still be indexed
     * using (x, y, c). Passing it to a generator requires that the
     * generator has been compiled with support for interleaved (also
     * known as packed or chunky) memory layouts. */
    static Image<T, D> make_interleaved(int width, int height, int channels) {
        static_assert(D >= 3, "Not enough dimensions to make an interleaved image");
        Image<T, D> im(channels, width, height);
        std::swap(im.dim[0], im.dim[1]);
        std::swap(im.dim[1], im.dim[2]);
        return im;
    }

private:
    template<typename ...Args>
    T *address_of_helper(int d, int first, Args... rest) const {
        return address_of_helper(d+1, rest...) + shape[d].stride * (first - shape[d].min);
    }

    T *address_of_helper(int d) const {
        return (T *)host;
    }

public:
    template<typename ...Args>
    T *address_of(int first, Args... rest) const {
        return address_of_helper(0, first, rest...);
    }

    /** Access a pixel. Make sure you've called copy_to_host before
     * you start accessing pixels directly. */
    template<typename ...Args>
    T &operator()(int first, Args... rest) {
        return *(address_of(first, rest...));
    }

    template<typename ...Args>
    T &operator()() {
        return (T *)host;
    }

    template<typename ...Args>
    const T &operator()(int first, Args... rest) const {
        return *(address_of(first, rest...));
    }

    template<typename ...Args>
    const T &operator()() const {
        return (T *)host;
    }

    /** Conventional names for the first three dimensions. */
    // @{
    int width() const {
        return (dimensions > 0) ? dim[0].extent : 1;
    }
    int height() const {
        return (dimensions > 1) ? dim[1].extent : 1;
    }
    int channels() const {
        return (dimensions > 2) ? dim[2].extent : 1;
    }
    // @}

    /** Make a new image which is a deep copy of this image. Use crop
     * or slice followed by copy to make a copy of only a portion of
     * the image. The new image uses the standard planar memory
     * layout. */
    Image<T, D> copy() const {
        Image<T, D> im = *this;
        im.allocate();
        uint8_t *dst = im.begin();
        for_every_contiguous_block(
            [&](uint8_t *begin, uint8_t *end) {
                size_t size = end - begin;
                memcpy(dst, begin, size);
                dst += size;
            });
        return im;
    }

    /** Make an image that refers to a sub-range of this image along
     * the given dimension. Does not take ownership of the data, but
     * contributes to its reference count. */
    Image<T, D> cropped(int d, int min, int extent) const {
        // Make a fresh copy of the underlying buffer (but not a fresh
        // copy of the allocation, if there is one).
        Image<T, D> im = *this;
        im.dim[d].min = min;
        im.dim[d].extent = extent;
        int shift = min - dim[d].min;
        im.host += shift * dim[d].stride * sizeof(T);
        return im;
    }

    /** Make an image which refers to the same data using a different
     * ordering of the dimensions. */
    Image<T, D> transposed(int d1, int d2) const {
        Image<T, D> im = *this;
        std::swap(im.dim[d1], im.dim[d2]);
        return im;
    }

    /** Make a lower-dimensional image that refers to one slice of this
     * image. */
    Image<T, D> sliced(int d, int pos) const {
        Image<T, D> im = *this;
        im.dimensions--;
        for (int i = d; i < im.dimensions; i++) {
            im.dim[i] = im.dim[i+1];
        }
        int shift = pos - dim[d].min;
        im.host += shift * dim[d].stride * sizeof(T);
        return im;
    }

    void copy_to_host() {
        if (device_dirty()) {
            halide_copy_to_host(NULL, this);
        }
    }

    void copy_to_device(const struct halide_device_interface_t *device_interface) {
        if (host_dirty()) {
            halide_copy_to_device(NULL, this, device_interface);
        }
    }

    void device_free() {
        halide_device_free(nullptr, this);
    }
};

}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_IMAGE_H

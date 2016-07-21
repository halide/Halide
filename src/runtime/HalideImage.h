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

template<typename Fn>
void for_each_element(const halide_buffer_t &buf, Fn f);

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

    T *address_of(const int *pos) const {
        return (T *)(halide_buffer_t::address_of(pos));
    }

    /** Access a pixel. Make sure you've called copy_to_host before
     * you start accessing pixels directly. */
    template<typename ...Args>
    T &operator()(int first, Args... rest) {
        return *(address_of(first, rest...));
    }

    template<typename ...Args>
    T &operator()() {
        return *((T *)host);
    }

    template<typename ...Args>
    const T &operator()(int first, Args... rest) const {
        return *(address_of(first, rest...));
    }

    template<typename ...Args>
    const T &operator()() const {
        return (T *)host;
    }

    const T &operator()(const int *pos) const {
        return *((T *)address_of(pos));
    }

    T &operator()(const int *pos) {
        return *((T *)address_of(pos));
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
     * the image. The new image uses the same memory layout as the
     * original, with holes compacted away. */
    Image<T, D> copy() const {
        Image<T, D> src = *this;

        // Reorder the dimensions of src to have strides in increasing order
        int swaps[(D*(D+1))/2];
        int swaps_idx = 0;
        for (int i = dimensions-1; i > 0; i--) {
            for (int j = i; j > 0; j--) {
                if (src.dim[j-1].stride > src.dim[j].stride) {
                    std::swap(src.dim[j-1], src.dim[j]);
                    swaps[swaps_idx++] = j;
                }
            }
        }

        // Make a copy of it using this dimension ordering
        Image<T, D> dst = src;
        dst.allocate();

        // Concatenate dense inner dimensions into contiguous memcpy tasks
        Image<T, D> src_slice = src;
        Image<T, D> dst_slice = dst;
        int64_t slice_size = 1;
        while (src_slice.dimensions && src_slice.dim[0].stride == slice_size) {
            assert(dst_slice.dim[0].stride == slice_size);
            slice_size *= src_slice.dim[0].stride;
            src_slice = src_slice.sliced(0, src_slice.dim[0].min);
            dst_slice = dst_slice.sliced(0, dst_slice.dim[0].min);
        }

        slice_size *= sizeof(T);
        // Do the memcpys
        src_slice.for_each_element([&](const int *pos) {
                memcpy(dst_slice.address_of(pos), src_slice.address_of(pos), slice_size);
            });

        // Undo the dimension reordering
        while (swaps_idx > 0) {
            int j = swaps[--swaps_idx];
            std::swap(dst.dim[j-1], dst.dim[j]);
        }

        return dst;
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

    /** Call a callable at each location within the image. See
     * for_each_element below for more details. */
    template<typename Fn>
    void for_each_element(Fn f) const {
        Halide::Tools::for_each_element(*this, f);
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

// We also define some helpers for iterating over buffers

template<typename Fn>
struct for_each_element_helpers {


    // If f is callable with this many args, call it. The first dummy
    // argument is to make this version preferable for overload
    // resolution. The decltype is to make this version impossible if
    // the function is not callable with this many args.
    template<typename ...Args>
    static auto for_each_element_variadic(int, int d, Fn f, const halide_buffer_t &buf, Args... args)
        -> decltype(f(args...)) {
        f(args...);
    }

    // Otherwise add an outer loop over an additional argument and
    // try again.
    template<typename ...Args>
    static void for_each_element_variadic(double, int d, Fn f, const halide_buffer_t &buf, Args... args) {
        for (int i = 0; i < std::max(1, buf.dim[d].extent); i++) {
            for_each_element_variadic(0, d-1, f, buf, buf.dim[d].min + i, args...);
        }
    }

    // Determine the minimum number of arguments a callable can take using the same trick.
    template<typename ...Args>
    static auto num_args(int, int *result, Fn f, Args... args) -> decltype(f(args...)) {
        *result = sizeof...(args);
    }

    // The recursive version is only enabled up to a recursion limit of 256
    template<typename ...Args>
    static void num_args(double, int *result, Fn f, Args... args) {
        static_assert(sizeof...(args) <= 256,
                      "Callable passed to for_each_element must accept either a const int *,"
                      " or up to 256 ints. No such operator found. Expect infinite template recursion.");
        return num_args(0, result, f, 0, args...);
    }

    static int get_number_of_args(Fn f) {
        int result;
        num_args(0, &result, f);
        return result;
    }

    // A run-time-recursive version (instead of
    // compile-time-recursive) that requires the callable to take a
    // pointer to a position array instead.
    static void for_each_element_array(int d, Fn f, const halide_buffer_t &buf, int *pos) {
        if (d == -1) {
            f(pos);
        } else {
            pos[d] = buf.dim[d].min;
            for (int i = 0; i < std::max(1, buf.dim[d].extent); i++) {
                for_each_element_array(d - 1, f, buf, pos);
                pos[d]++;
            }
        }
    }

    // If the callable takes a pointer, use the array version
    template<typename Fn2>
    static auto for_each_element(int, const halide_buffer_t &buf, Fn2 f)
        -> decltype(f((const int *)0)) {
        int pos[buf.dimensions];
        memset(pos, 0, sizeof(int) * buf.dimensions);
        for_each_element_array(buf.dimensions - 1, f, buf, pos);
    }

    // Otherwise try the variadic version
    template<typename Fn2>
    static void for_each_element(double, const halide_buffer_t &buf, Fn2 f) {
        int num_args = get_number_of_args(f);
        for_each_element_variadic(0, num_args-1, f, buf);
    }
};

/** Call a function at each site in a buffer. If the function has more
 * arguments than the buffer has dimensions, the remaining arguments
 * will be zero. If it has fewer arguments than the buffer has
 * dimensions then the last few dimensions of the buffer are not
 * iterated over. For example, the following code exploits this to set
 * a floating point RGB image to red:

\code
Image<float, 3> im(100, 100, 3);
for_each_element(im, [&](int x, int y) {
    im(x, y, 0) = 1.0f;
    im(x, y, 1) = 0.0f;
    im(x, y, 2) = 0.0f:
});
\endcode

 * The compiled code is equivalent to writing the a nested for loop,
 * and compilers are capable of optimizing it in the same way.
 *
 * If the callable can be called with an int * as the sole argument,
 * that version is called instead, with each element in the buffer
 * being passed to it in a coordinate array. This is higher-overhead,
 * but is useful for writing generic code. For example, the following
 * sets the value at all sites in an arbitrary-dimensional buffer to
 * their first coordinate:

\code
for_each_element(im, [&](const int *pos) {im(pos) = pos[0];});
\endcode

* It is also possible to use for_each_element to iterate over entire
* rows or columns by cropping the buffer to a single column or row
* respectively and iterating over elements of the result. For example,
* to set the diagonal of the image to 1 by iterating over the columns:

\code
Image<float, 3> im(100, 100, 3);
for_each_element(im.sliced(1, 0), [&](int x, int c) {
    im(x, x, c) = 1.0f;
});
\endcode

* Or, assuming the memory layout is known, one can memset each row
* like so:

Image<float, 3> im(100, 100, 3);
for_each_element(im.sliced(0, 0), [&](int y, int c) {
    memset(im.address_of(0, y, c), 0, sizeof(float) * im.width());
});


\endcode

*/
template<typename Fn>
void for_each_element(const halide_buffer_t &buf, Fn f) {
    for_each_element_helpers<Fn>::for_each_element(0, buf, f);
}

}  // namespace Tools
}  // namespace Halide

#endif  // HALIDE_RUNTIME_IMAGE_H

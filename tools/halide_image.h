// A simple Image class which wraps a buffer_t. This is useful
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
void for_each_element(const buffer_t &buf, Fn f);

/** A templated Image class derived from buffer_t that adds
 * functionality. T is the element type, and D is the maximum number
 * of dimensions. It must be less than or equal to 4 until we have
 * support for higher-dimensional buffers. */
template<typename T, int D = 4, void *(*Allocate)(size_t) = malloc, void (*Deallocate)(void *) = free>
class Image : public buffer_t {
    static_assert(D <= 4, "buffer_t supports a maximum of four dimensions");

    /** The upcoming buffer_t upgrade to halide_buffer_t stores the
     * shape in an array of halide_dimension_t objects. We use a
     * similar array of *references* to the buffer shape so that we
     * can start porting code to halide_buffer_t's interface. */
    struct halide_dimension_t {
        int &min, &stride, &extent;
    } shape[4];

    /** The allocation owned by this Image. NULL if the Image does not
     * own the memory. */
    std::shared_ptr<uint8_t> alloc;

    /** A temporary helper function to get the number of dimensions in
     * a buffer_t. Will disappear when halide_buffer_t is merged. */
    int buffer_dimensions(const buffer_t &buf) {
        for (int d = 0; d < 4; d++) {
            if (buf.extent[d] == 0) {
                return d;
            }
        }
        return 4;
    }

    /** Initialize the shape from a buffer_t. */
    void initialize_from_buffer(const buffer_t &buf) {
        dimensions = buffer_dimensions(buf);
        assert(dimensions <= D);
        memcpy((buffer_t *)this, &buf, sizeof(buffer_t));
    }

    /** Initialize the shape from a parameter pack of ints */
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

    /** Base case for the template recursion above. */
    void initialize_shape(halide_dimension_t *) {
    }

    /** Initialize the shape from the static shape of an array */
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

    /** Base case for the template recursion above. */
    void initialize_shape_from_array_shape(halide_dimension_t *, T &) {
    }

    /** Get the dimensionality of a multi-dimensional C array */
    template<typename Array, size_t N>
    static int dimensionality_of_array(Array (&vals)[N]) {
        return dimensionality_of_array(vals[0]) + 1;
    }

    static int dimensionality_of_array(T &) {
        return 0;
    }

    /** Check if any args in a parameter pack are zero */
    template<typename ...Args>
    static bool any_zero(int first, Args... rest) {
        if (first == 0) return true;
        return any_zero(rest...);
    }

    static bool any_zero() {
        return false;
    }

public:
    typedef T ElemType;

    /** Public fields that halide_buffer_t has that buffer_t does not have. */

    /** The dimensionality of the buffer */
    int dimensions;

    /** The halide type of the elements */
    halide_type_t type;

    /** halide_buffer_t accesses its shape via a pointer to an array
     * of dimensions */
    halide_dimension_t *dim = shape;

    /** The total number of elements this buffer represents. Equal to
     * the product of the extents */
    size_t number_of_elements() const {
        size_t s = 1;
        for (int i = 0; i < dimensions; i++) {
            s *= dim[i].extent;
        }
        return s;
    }

    /** A pointer to the element with the lowest address. If all
     * strides are positive, equal to the host pointer. */
    uint8_t *begin() const {
        ptrdiff_t index = 0;
        for (int i = 0; i < dimensions; i++) {
            if (dim[i].stride < 0) {
                index += dim[i].stride * (dim[i].extent - 1);
            }
        }
        return host + index * type.bytes();
    }

    /** A pointer to one beyond the element with the highest address. */
    uint8_t *end() const {
        ptrdiff_t index = 0;
        for (int i = 0; i < dimensions; i++) {
            if (dim[i].stride > 0) {
                index += dim[i].stride * (dim[i].extent - 1);
            }
        }
        index += 1;
        return host + index * type.bytes();
    }

    /** The total number of bytes spanned by the data in memory. */
    size_t size_in_bytes() const {
        return (size_t)(end() - begin());
    }

    /** We need custom constructors and assignment operators so that
     * we can ensure the dim pointer always points to this Image's
     * shape array, and so that the shape array points to the actual
     * mins, strides, and extents. */
    Image() :
        shape {{min[0], stride[0], extent[0]},
               {min[1], stride[1], extent[1]},
               {min[2], stride[2], extent[2]},
               {min[3], stride[3], extent[3]}},
        dimensions(0),
        type(halide_type_of<T>()),
        dim(&shape[0]) {
        memset((buffer_t *)this, 0, sizeof(buffer_t));
    }

    Image(const Image<T, D> &other) :
        shape {{min[0], stride[0], extent[0]},
               {min[1], stride[1], extent[1]},
               {min[2], stride[2], extent[2]},
               {min[3], stride[3], extent[3]}},
        alloc(other.alloc),
        type(halide_type_of<T>()),
        dim(&shape[0]) {
        initialize_from_buffer(other);
    }

    Image(const Image<T, D> &&other) :
        shape {{min[0], stride[0], extent[0]},
               {min[1], stride[1], extent[1]},
               {min[2], stride[2], extent[2]},
               {min[3], stride[3], extent[3]}},
        alloc(std::move(other.alloc)),
        type(halide_type_of<T>()),
        dim(&shape[0]) {
        initialize_from_buffer(other);
    }

    Image<T, D> &operator=(const Image<T, D> &other) {
        initialize_from_buffer(other);
        dimensions = other.dimensions;
        type = other.type;
        alloc = other.alloc;
        return *this;
    }

    Image<T, D> &operator=(const Image<T, D> &&other) {
        initialize_from_buffer(other);
        dimensions = other.dimensions;
        type = other.type;
        alloc = std::move(other.alloc);
        return *this;
    }

    Image(const buffer_t &buf) :
        shape {{min[0], stride[0], extent[0]},
               {min[1], stride[1], extent[1]},
               {min[2], stride[2], extent[2]},
               {min[3], stride[3], extent[3]}},
        type(halide_type_of<T>()) {
        initialize_from_buffer(buf);
    }

    /** Allocate memory for this Image. Drops the reference to any
     * existing memory. */
    void allocate() {
        assert(dev == 0);

        // Conservatively align images to 128 bytes. This is enough
        // alignment for all the platforms we might use.
        size_t size = size_in_bytes();
        const size_t alignment = 128;
        size = (size + alignment - 1) & ~(alignment - 1);
        uint8_t *ptr = (uint8_t *)Allocate(sizeof(T)*size + alignment - 1);
        host = (uint8_t *)((uintptr_t)(ptr + alignment - 1) & ~(alignment - 1));
        alloc.reset(ptr, Deallocate);
    }

    /** Allocate a new image of the given size. Pass zeroes to make a
     * buffer suitable for bounds query calls. */
    template<typename ...Args>
    Image(int first, Args... rest) :
        shape {{min[0], stride[0], extent[0]},
               {min[1], stride[1], extent[1]},
               {min[2], stride[2], extent[2]},
               {min[3], stride[3], extent[3]}} {
        static_assert(sizeof...(rest) < D,
                      "Too many arguments to constructor. Use Image<T, D>, where D is at least the desired number of dimensions");
        memset((buffer_t *)this, 0, sizeof(buffer_t));
        initialize_shape(shape, first, rest...);
        type = halide_type_of<T>();
        elem_size = type.bytes();
        dimensions = 1 + (int)(sizeof...(rest));
        dim = &shape[0];
        if (!any_zero(first, rest...)) {
            allocate();
        }
    }

    /** Make an image that refers to a statically sized array. Does not
     * take ownership of the data. */
    template<typename Array, size_t N>
    explicit Image(Array (&vals)[N]) :
        shape {{min[0], stride[0], extent[0]},
               {min[1], stride[1], extent[1]},
               {min[2], stride[2], extent[2]},
               {min[3], stride[3], extent[3]}} {
        memset((buffer_t *)this, 0, sizeof(buffer_t));
        dimensions = dimensionality_of_array(vals);
        initialize_shape_from_array_shape(shape + dimensions - 1, vals);
        type = halide_type_of<T>();
        elem_size = type.bytes();
        dim = &shape[0];
        host = (uint8_t *)vals;
    }

    /** Initialize an Image from a pointer and some sizes. Assumes
     * dense row-major packing. Does not take ownership of the data. */
    template<typename ...Args>
    explicit Image(T *data, int first, Args... rest) :
        shape {{min[0], stride[0], extent[0]},
               {min[1], stride[1], extent[1]},
               {min[2], stride[2], extent[2]},
               {min[3], stride[3], extent[3]}} {
        static_assert(sizeof...(rest) < D,
                      "Too many arguments to constructor. Use Image<T, D>, where D is at least the desired number of dimensions");
        memset((buffer_t *)this, 0, sizeof(buffer_t));
        initialize_shape(0, first, rest...);
        type = halide_type_of<T>();
        elem_size = type.bytes();
        dimensions = 1 + (int)(sizeof...(rest));
        dim = &shape[0];
        host = data;
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
        std::swap(im.min[0], im.min[1]);
        std::swap(im.min[1], im.min[2]);
        std::swap(im.extent[0], im.extent[1]);
        std::swap(im.extent[1], im.extent[2]);
        std::swap(im.stride[0], im.stride[1]);
        std::swap(im.stride[1], im.stride[2]);
        return im;
    }

private:
    template<typename ...Args>
    T *address_of(int d, int first, Args... rest) const {
        return address_of(d+1, rest...) + stride[d] * (first - min[d]);
    }
    
    T *address_of(int d) const {
        return (T *)host;
    }

    T *address_of(const int *pos) const {
        T *ptr = (T *)host;        
        for (int i = dimensions-1; i >= 0; i--) {
            ptr += dim[i].stride * (pos[i] - dim[i].min);
        }
        return ptr;
    }
public:

    /** Access pixels. Note that while these methods are marked const,
     * they return non-const references. 
     */
    //@{
    template<typename ...Args>
    const T &operator()(int first, Args... rest) const {
        return *(address_of(0, first, rest...));
    }

    template<typename ...Args>
    const T &operator()() const {
        return *((T *)host);
    }

    const T &operator()(const int *pos) const {
        return *((T *)address_of(pos));
    }

    template<typename ...Args>
    T &operator()(int first, Args... rest) {
        return *(address_of(0, first, rest...));
    }

    template<typename ...Args>
    T &operator()() {
        return *((T *)host);
    }

    T &operator()(const int *pos) {
        return *((T *)address_of(pos));
    }    
    // @}

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
                memcpy(&dst_slice(pos), &src_slice(pos), slice_size);
            });

        // Undo the dimension reordering
        while (swaps_idx > 0) {
            int j = swaps[--swaps_idx];
            std::swap(dst.dim[j-1], dst.dim[j]);
        }

        return dst;
    }

    /** Make an image that refers to a sub-range of this image along
     * the given dimension. Does not assert the crop region is within
     * the existing bounds. */
    Image<T, D> cropped(int d, int min, int extent) const {
        assert(dev == 0);
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
        assert(dev == 0);
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

    /** Methods for managing any GPU allocation. */
    // @{
    void set_host_dirty(bool v = true) {
        host_dirty = v;
    }

    bool device_dirty() const {
        return dev_dirty;
    }

    // In the future there will also be a host_dirty() method, but
    // currently it conflicts with a buffer_t field.

    void set_device_dirty(bool v = true) {
        dev_dirty = v;
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
    // @}
};

// We also define some helpers for iterating over buffers and calling a callable at each site.
template<typename Fn>
struct for_each_element_helpers {

    /** If f is callable with this many args, call it. The first dummy
     * argument is to make this version preferable for overload
     * resolution. The decltype is to make this version impossible if
     * the function is not callable with this many args. */
    template<typename ...Args>
    static auto for_each_element_variadic(int, int d, Fn f, const buffer_t &buf, Args... args)
        -> decltype(f(args...)) {
        f(args...);
    }

    /** If the above overload is impossible, we add an outer loop over
     * an additional argument and try again. This trick is known as
     * SFINAE. */
    template<typename ...Args>
    static void for_each_element_variadic(double, int d, Fn f, const buffer_t &buf, Args... args) {
        for (int i = 0; i < std::max(1, buf.extent[d]); i++) {
            for_each_element_variadic(0, d-1, f, buf, buf.min[d] + i, args...);
        }
    }

    /** Determine the minimum number of arguments a callable can take
     * using the same trick. */
    template<typename ...Args>
    static auto num_args(int, int *result, Fn f, Args... args) -> decltype(f(args...)) {
        *result = sizeof...(args);
    }

    /** The recursive version is only enabled up to a recursion limit
     * of 256. This catches callables that aren't callable with any
     * number of ints. */
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

    /** A version where the callable takes a position array instead,
     * with compile-time recursion on the dimensionality.  This
     * overload is preferred to the one below using the same int vs
     * double trick as above, but is impossible once d hits -1 using
     * std::enable_if. */
    template<int d>
    static typename std::enable_if<d >= 0, void>::type
    for_each_element_array_helper(int, Fn f, const buffer_t &buf, int *pos) {
        for (pos[d] = buf.min[d]; pos[d] < buf.min[d] + buf.extent[d]; pos[d]++) {
            for_each_element_array_helper<d - 1>(0, f, buf, pos);
        }
    }

    /** Base case for recursion above. */
    template<int d>
    static void for_each_element_array_helper(double, Fn f, const buffer_t &buf, int *pos) {
        f(pos);
    }    

    
    /** A run-time-recursive version (instead of
     * compile-time-recursive) that requires the callable to take a
     * pointer to a position array instead. Dispatches to the
     * compile-time-recursive version once the dimensionality gets
     * small. */
    static void for_each_element_array(int d, Fn f, const buffer_t &buf, int *pos) {
        if (d == -1) {
            f(pos);
        } else if (d == 0) {
            // Once the dimensionality gets small enough, dispatch to
            // a compile-time-recursive version for better codegen of
            // the inner loops.
            for_each_element_array_helper<0>(0, f, buf, pos);
        } else if (d == 1) {
            for_each_element_array_helper<1>(0, f, buf, pos);
        } else if (d == 2) {
            for_each_element_array_helper<2>(0, f, buf, pos);
        } else if (d == 3) {
            for_each_element_array_helper<3>(0, f, buf, pos);                        
        } else {
            for (pos[d] = buf.min[d]; pos[d] < buf.min[d] + buf.extent[d]; pos[d]++) {
                for_each_element_array(d - 1, f, buf, pos);
            }
        }
    }

    /** We now have two overloads for for_each_element. This one
     * triggers if the callable takes a const int *.
     */
    template<typename Fn2>
    static auto for_each_element(int, const buffer_t &buf, Fn2 f)
        -> decltype(f((const int *)0)) {
        int pos[4] = {0, 0, 0, 0};
        int dimensions = 0;
        while (buf.extent[dimensions] != 0 && dimensions < 4) {
            dimensions++;
        }
        for_each_element_array(dimensions - 1, f, buf, pos);
    }

    /** This one triggers otherwise. It treats the callable as
     * something that takes some number of ints. */
    template<typename Fn2>
    static void for_each_element(double, const buffer_t &buf, Fn2 f) {
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
 * that version is called instead. Each location in the buffer is
 * passed to it in a coordinate array. This version is higher-overhead
 * than the variadic version, but is useful for writing generic code
 * that accepts buffers of arbitrary dimensionality. For example, the
 * following sets the value at all sites in an arbitrary-dimensional
 * buffer to their first coordinate:

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

* Or, assuming the memory layout is known to be dense per row, one can
* memset each row of an image like so:

Image<float, 3> im(100, 100, 3);
for_each_element(im.sliced(0, 0), [&](int y, int c) {
    memset(&im(0, y, c), 0, sizeof(float) * im.width());
});


\endcode

*/
template<typename Fn>
void for_each_element(const buffer_t &buf, Fn f) {
    for_each_element_helpers<Fn>::for_each_element(0, buf, f);
}

}  // namespace Tools
}  // namespace Halide

#endif  // HALIDE_RUNTIME_IMAGE_H

/** \file
 * Defines an Image type that wraps from buffer_t and adds
 * functionality, and methods for more conveniently iterating over the
 * samples in a buffer_t outside of Halide code. */

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

/** A templated Image class that wraps from buffer_t and adds
 * functionality. When using Halide from C++, this is the preferred
 * way to create input and output buffers. The overhead of using this
 * class relative to a naked buffer_t is minimal - it uses another
 * ~100 bytes on the stack, and does no dynamic allocations when using
 * it to represent existing memory. This overhead will shrink further
 * in the future once buffer_t is deprecated.
 *
 * The template parameter T is the element type, and D is the maximum
 * number of dimensions. It must be less than or equal to 4 for now.
 *
 * The class optionally allocates and owns memory for the image using
 * a std::shared_ptr allocated with the provided allocator. If they
 * are null, malloc and free are used.  Any device-side allocation is
 * not owned, and must be freed manually using device_free.
 *
 * For accessing the shape and type, this class provides both the
 * buffer_t interface (extent[i], min[i], and stride[i] arrays, the
 * elem_size field), and also the interface of the yet-to-come
 * halide_buffer_t, which will replace buffer_t. This is intended to
 * allow a gradual transition to halide_buffer_t. New code should
 * access the shape via dim[i].extent, dim[i].min, dim[i].stride, and
 * the type via the 'type' field. */
template<typename T, int D = 4, void *(*Allocate)(size_t) = nullptr, void (*Deallocate)(void *) = nullptr>
class Image {
    static_assert(D <= 4, "buffer_t supports a maximum of four dimensions");

    buffer_t buf = {0};

    /** Read-only access to the shape */
    class Dimension {
        const buffer_t &buf;
        const int idx;
    public:
        int min() const {
            return buf.min[idx];
        }
        int stride() const {
            return buf.stride[idx];
        }
        int extent() const {
            return buf.extent[idx];
        }
        int max() const {
            return min() + extent() - 1;;
        }
        Dimension(const buffer_t &buf, int idx) : buf(buf), idx(idx) {}
    };

    /** Fields that halide_buffer_t has that buffer_t does not have. */

    /** The dimensionality of the buffer */
    int dims = 0;

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
    void initialize_from_buffer(const buffer_t &b) {
        dims = buffer_dimensions(b);
        assert(dims <= D);
        memcpy(&buf, &b, sizeof(buffer_t));
    }

    /** Initialize the shape from a parameter pack of ints */
    template<typename ...Args>
    void initialize_shape(int next, int first, const Args &... rest) {
        buf.min[next] = 0;
        buf.extent[next] = first;
        if (next == 0) {
            buf.stride[next] = 1;
        } else {
            buf.stride[next] = buf.stride[next-1] * buf.extent[next-1];
        }
        initialize_shape(next + 1, rest...);
    }

    /** Base case for the template recursion above. */
    void initialize_shape(int) {
    }

    /** Initialize the shape from the static shape of an array */
    template<typename Array, size_t N>
    void initialize_shape_from_array_shape(int next, Array (&vals)[N]) {
        buf.min[next] = 0;
        buf.extent[next] = (int)N;
        if (next == 0) {
            buf.stride[next] = 1;
        } else {
            initialize_shape_from_array_shape(next - 1, vals[0]);
            buf.stride[next] = buf.stride[next - 1] * buf.extent[next - 1];
        }
    }

    /** Base case for the template recursion above. */
    void initialize_shape_from_array_shape(int, T &) {
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
    static bool any_zero(int first, const Args &... rest) {
        if (first == 0) return true;
        return any_zero(rest...);
    }

    static bool any_zero() {
        return false;
    }

public:
    typedef T ElemType;

    /** Access the shape of the buffer */
    Dimension dim(int i) const {
        return Dimension(buf, i);
    }

    /** The total number of elements this buffer represents. Equal to
     * the product of the extents */
    size_t number_of_elements() const {
        size_t s = 1;
        for (int i = 0; i < dimensions(); i++) {
            s *= dim(i).extent();
        }
        return s;
    }

    /** Get the dimensionality of the buffer. */
    int dimensions() const {
        return dims;
    }

    /** Get the type of the elements. */
    halide_type_t type() const {
        return halide_type_of<T>();
    }

    /** A pointer to the element with the lowest address. If all
     * strides are positive, equal to the host pointer. */
    uint8_t *begin() const {
        ptrdiff_t index = 0;
        for (int i = 0; i < dimensions(); i++) {
            if (dim(i).stride() < 0) {
                index += dim(i).stride() * (dim(i).extent() - 1);
            }
        }
        return buf.host + index * sizeof(T);
    }

    /** A pointer to one beyond the element with the highest address. */
    uint8_t *end() const {
        ptrdiff_t index = 0;
        for (int i = 0; i < dimensions(); i++) {
            if (dim(i).stride() > 0) {
                index += dim(i).stride() * (dim(i).extent() - 1);
            }
        }
        index += 1;
        return buf.host + index * sizeof(T);
    }

    /** The total number of bytes spanned by the data in memory. */
    size_t size_in_bytes() const {
        return (size_t)(end() - begin());
    }


    Image() {}

    Image(const buffer_t &buf) {
        initialize_from_buffer(buf);
    }

    /** Allocate memory for this Image. Drops the reference to any
     * existing memory. */
    void allocate() {
        assert(buf.dev == 0);

        // Conservatively align images to 128 bytes. This is enough
        // alignment for all the platforms we might use.
        size_t size = size_in_bytes();
        const size_t alignment = 128;
        size = (size + alignment - 1) & ~(alignment - 1);
        uint8_t *ptr;
        if (Allocate != nullptr) {
            ptr = (uint8_t *)Allocate(size + alignment - 1);
            alloc.reset(ptr, Deallocate);
        } else {
            ptr = (uint8_t *)malloc(size + alignment - 1);
            alloc.reset(ptr, free);
        }
        buf.host = (uint8_t *)((uintptr_t)(ptr + alignment - 1) & ~(alignment - 1));
    }

    /** Allocate a new image of the given size. Pass zeroes to make a
     * buffer suitable for bounds query calls. */
    template<typename ...Args>
    Image(int first, const Args &... rest) {
        static_assert(sizeof...(rest) < D,
                      "Too many arguments to constructor. Use Image<T, D>, where D is at least the desired number of dimensions");
        initialize_shape(0, first, rest...);
        buf.elem_size = sizeof(T);
        dims = 1 + (int)(sizeof...(rest));
        if (!any_zero(first, rest...)) {
            allocate();
        }
    }

    /** Make an image that refers to a statically sized array. Does not
     * take ownership of the data. */
    template<typename Array, size_t N>
    explicit Image(Array (&vals)[N]) {
        dims = dimensionality_of_array(vals);
        initialize_shape_from_array_shape(dims - 1, vals);
        buf.elem_size = sizeof(T);
        buf.host = (uint8_t *)vals;
    }

    /** Initialize an Image from a pointer and some sizes. Assumes
     * dense row-major packing and a min coordinate of zero. Does not
     * take ownership of the data. */
    template<typename ...Args>
    explicit Image(T *data, int first, const Args &... rest) {
        static_assert(sizeof...(rest) < D,
                      "Too many arguments to constructor. Use Image<T, D>, where D is at least the desired number of dimensions");
        memset(&buf, 0, sizeof(buffer_t));
        initialize_shape(0, first, rest...);
        buf.elem_size = sizeof(T);
        dims = 1 + (int)(sizeof...(rest));
        buf.host = data;
    }

    /** A C struct describing the shape of a single dimension. This
     * will be a type in the runtime once halide_buffer_t is
     * merged. */
    struct halide_dimension_t {
        int min, extent, stride;
    };

    /** Initialize an Image from a pointer to the min coordinate and
     * an array describing the shape.  Does not take ownership of the
     * data. */
    template<int N>
    explicit Image(T *data, halide_dimension_t shape[N]) {
        static_assert(N <= D,
                      "Too many arguments to constructor. Use Image<T, D>, where D is at least the desired number of dimensions");
        memset(&buf, 0, sizeof(buffer_t));
        for (int i = 0; i < N; i++) {
            buf.min[i]    = shape[i].min;
            buf.extent[i] = shape[i].extent;
            buf.stride[i] = shape[i].stride;
        }
        buf.elem_size = sizeof(T);
        dims = N;
        buf.host = data;
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
        std::swap(im.buf.min[0], im.buf.min[1]);
        std::swap(im.buf.min[1], im.buf.min[2]);
        std::swap(im.buf.extent[0], im.buf.extent[1]);
        std::swap(im.buf.extent[1], im.buf.extent[2]);
        std::swap(im.buf.stride[0], im.buf.stride[1]);
        std::swap(im.buf.stride[1], im.buf.stride[2]);
        return im;
    }

private:
    template<typename ...Args>
    T *address_of(int d, int first, const Args &... rest) const {
        return address_of(d+1, rest...) + buf.stride[d] * (first - buf.min[d]);
    }

    T *address_of(int d) const {
        return (T *)buf.host;
    }

    T *address_of(const int *pos) const {
        T *ptr = (T *)buf.host;
        for (int i = dimensions() - 1; i >= 0; i--) {
            ptr += dim(i).stride() * (pos[i] - dim(i).min());
        }
        return ptr;
    }
public:

    /** Get a pointer to the raw buffer_t this wraps. */
    // @{
    buffer_t *raw_buffer() {
        return &buf;
    }

    const buffer_t *raw_buffer() const {
        return &buf;
    }
    // @}

    /** Get a pointer to the address of the min coordinate. */
    // @{
    T *data() {
        return (T *)buf.host;
    }

    const T *data() const {
        return (const T *)buf.host;
    }
    // @}
    
    /** Provide a cast operator to buffer_t *, so that instances can
     * be passed directly to Halide filters. */
    operator buffer_t *() {
        return &buf;
    }

    /** Access elements. Use im(...) to get a reference to an element, and
     * use &im(...) to get the address of an element.
     */
    //@{
    template<typename ...Args>
    const T &operator()(int first, const Args &... rest) const {
        return *(address_of(0, first, rest...));
    }

    template<typename ...Args>
    const T &operator()() const {
        return *((T *)buf.host);
    }

    const T &operator()(const int *pos) const {
        return *((T *)address_of(pos));
    }

    template<typename ...Args>
    T &operator()(int first, const Args &... rest) {
        return *(address_of(0, first, rest...));
    }

    template<typename ...Args>
    T &operator()() {
        return *((T *)buf.host);
    }

    T &operator()(const int *pos) {
        return *((T *)address_of(pos));
    }
    // @}

    /** Conventional names for the first three dimensions. */
    // @{
    int width() const {
        return (dimensions() > 0) ? dim(0).extent() : 1;
    }
    int height() const {
        return (dimensions() > 1) ? dim(1).extent() : 1;
    }
    int channels() const {
        return (dimensions() > 2) ? dim(2).extent() : 1;
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
        for (int i = dimensions()-1; i > 0; i--) {
            for (int j = i; j > 0; j--) {
                if (src.dim(j-1).stride() > src.dim(j).stride()) {
                    std::swap(src.buf.min[j-1], src.buf.min[j]);
                    std::swap(src.buf.stride[j-1], src.buf.stride[j]);
                    std::swap(src.buf.extent[j-1], src.buf.extent[j]);
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
        while (src_slice.dimensions && src_slice.dim(0).stride() == slice_size) {
            assert(dst_slice.dim(0).stride() == slice_size);
            slice_size *= src_slice.dim(0).stride();
            src_slice = src_slice.sliced(0, src_slice.dim(0).min());
            dst_slice = dst_slice.sliced(0, dst_slice.dim(0).min());
        }

        slice_size *= sizeof(T);
        // Do the memcpys
        src_slice.for_each_element([&](const int *pos) {
                memcpy(&dst_slice(pos), &src_slice(pos), slice_size);
            });

        // Undo the dimension reordering
        while (swaps_idx > 0) {
            int j = swaps[--swaps_idx];
            std::swap(dst.buf.min[j-1], dst.buf.min[j]);
            std::swap(dst.buf.stride[j-1], dst.buf.stride[j]);
            std::swap(dst.buf.extent[j-1], dst.buf.extent[j]);
        }

        return dst;
    }

    /** Make an image that refers to a sub-range of this image along
     * the given dimension. Does not assert the crop region is within
     * the existing bounds. */
    Image<T, D> cropped(int d, int min, int extent) const {
        assert(buf.dev == 0);
        // Make a fresh copy of the underlying buffer (but not a fresh
        // copy of the allocation, if there is one).
        Image<T, D> im = *this;
        im.buf.min[d] = min;
        im.buf.extent[d] = extent;
        int shift = min - dim(d).min();
        im.buf.host += shift * dim(d).stride() * sizeof(T);
        return im;
    }

    /** Make an image which refers to the same data using a different
     * ordering of the dimensions. */
    Image<T, D> transposed(int d1, int d2) const {
        Image<T, D> im = *this;
        std::swap(im.shape[d1], im.shape[d2]);
        return im;
    }

    /** Make a lower-dimensional image that refers to one slice of this
     * image. */
    Image<T, D> sliced(int d, int pos) const {
        assert(buf.dev == 0);
        Image<T, D> im = *this;
        im.dimensions--;
        for (int i = d; i < im.dimensions; i++) {
            buf.stride[i] = buf.stride[i+1];
            buf.extent[i] = buf.extent[i+1];
            buf.min[i] = buf.min[i+1];
        }
        int shift = pos - dim(d).min();
        im.buf.host += shift * dim(d).stride() * sizeof(T);
        return im;
    }

    /** Call a callable at each location within the image. See
     * for_each_element below for more details. */
    template<typename Fn>
    void for_each_element(Fn f) const {
        Halide::Tools::for_each_element(buf, f);
    }

    /** Methods for managing any GPU allocation. */
    // @{
    void set_host_dirty(bool v = true) {
        buf.host_dirty = v;
    }

    bool device_dirty() const {
        return buf.dev_dirty;
    }

    bool host_dirty() const {
        return buf.host_dirty;
    }

    void set_device_dirty(bool v = true) {
        buf.dev_dirty = v;
    }

    void copy_to_host() {
        if (device_dirty()) {
            halide_copy_to_host(NULL, &buf);
        }
    }

    void copy_to_device(const struct halide_device_interface *device_interface) {
        if (host_dirty()) {
            halide_copy_to_device(NULL, &buf, device_interface);
        }
    }

    void device_free() {
        halide_device_free(nullptr, &buf);
    }
    // @}
};

/** Some helpers for for_each_element. */
template<typename Fn>
struct for_each_element_helpers {

    /** If f is callable with this many args, call it. The first dummy
     * argument is to make this version preferable for overload
     * resolution. The decltype is to make this version impossible if
     * the function is not callable with this many args. */
    template<typename ...Args>
    static auto for_each_element_variadic(int, int d, Fn f, const buffer_t &buf, const Args &... args)
        -> decltype(f(args...)) {
        f(args...);
    }

    /** If the above overload is impossible, we add an outer loop over
     * an additional argument and try again. This trick is known as
     * SFINAE. */
    template<typename ...Args>
    static void for_each_element_variadic(double, int d, Fn f, const buffer_t &buf, const Args &... args) {
        int e = buf.extent[d] == 0 ? 1 : buf.extent[d];
        for (int i = 0; i < e; i++) {
            for_each_element_variadic(0, d-1, f, buf, buf.min[d] + i, args...);
        }
    }

    /** Determine the minimum number of arguments a callable can take
     * using the same trick. */
    template<typename ...Args>
    static auto num_args(int, int *result, Fn f, const Args &... args) -> decltype(f(args...)) {
        *result = sizeof...(args);
    }

    /** The recursive version is only enabled up to a recursion limit
     * of 256. This catches callables that aren't callable with any
     * number of ints. */
    template<typename ...Args>
    static void num_args(double, int *result, Fn f, const Args &... args) {
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

/** Call a function at each site in a buffer. This is likely to be
 * much slower than using Halide code to populate a buffer, but is
 * convenient for tests. If the function has more arguments than the
 * buffer has dimensions, the remaining arguments will be zero. If it
 * has fewer arguments than the buffer has dimensions then the last
 * few dimensions of the buffer are not iterated over. For example,
 * the following code exploits this to set a floating point RGB image
 * to red:

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

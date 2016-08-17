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

/** A C struct describing the shape of a single dimension of a halide
 * buffer. This will be a type in the runtime once halide_buffer_t is
 * merged. */
struct halide_dimension_t {
    int min, extent, stride;
};


namespace Halide {
namespace Tools {

template<typename Fn>
void for_each_element(const buffer_t &buf, Fn &&f);

/** A class that wraps buffer_t and adds functionality. Acts as a base
 * class for the typed version below. Templated on the maximum
 * dimensionality it supports. Use it only when the the element type
 * is unknown, or generic. See the comments on the Image class below
 * for more details. */
template<int D = 4>
class Buffer {
    static_assert(D <= 4, "buffer_t supports a maximum of four dimensions");

protected:

    buffer_t buf = {0};

    /** Fields that halide_buffer_t has that buffer_t does not have. */

    /** The dimensionality of the buffer */
    int dims = 0;

    /** The type of the elements */
    halide_type_t ty;

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
    void initialize_shape(int next, int first, Args... rest) {
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
    template<typename T>
    void initialize_shape_from_array_shape(int, const T &) {
    }

    /** Get the dimensionality of a multi-dimensional C array */
    template<typename Array, size_t N>
    static int dimensionality_of_array(Array (&vals)[N]) {
        return Buffer<D>::dimensionality_of_array(vals[0]) + 1;
    }

    template<typename T>
    static int dimensionality_of_array(const T &) {
        return 0;
    }

    /** Get the underlying halide_type_t of an array's element type. */
    template<typename Array, size_t N>
    static halide_type_t scalar_type_of_array(Array (&vals)[N]) {
        return Buffer<D>::scalar_type_of_array(vals[0]);
    }

    template<typename T>
    static halide_type_t scalar_type_of_array(const T &) {
        return halide_type_of<typename std::remove_cv<T>::type>();
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

    /** Read-only access to the shape */
    class Dimension {
        const buffer_t &buf;
        const int idx;
    public:
        __attribute__((always_inline)) int min() const {
            return buf.min[idx];
        }
        __attribute__((always_inline)) int stride() const {
            return buf.stride[idx];
        }
        __attribute__((always_inline)) int extent() const {
            return buf.extent[idx];
        }
        __attribute__((always_inline)) int max() const {
            return min() + extent() - 1;
        }
        __attribute__((always_inline)) int begin() const {
            return min();
        }
        __attribute__((always_inline)) int end() const {
            return min() + extent();
        }
        Dimension(const buffer_t &buf, int idx) : buf(buf), idx(idx) {}
    };

    /** Access the shape of the buffer */
    __attribute__((always_inline)) Dimension dim(int i) const {
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
        return ty;
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
        return buf.host + index * buf.elem_size;
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
        return buf.host + index * buf.elem_size;
    }

    /** The total number of bytes spanned by the data in memory. */
    size_t size_in_bytes() const {
        return (size_t)(end() - begin());
    }

    Buffer() {}

    /** Make a buffer from a buffer_t */
    Buffer(const buffer_t &buf) {
        initialize_from_buffer(buf);
    }

    /** Give Buffers access to the members of Buffers of different dimensionalities. */
    template<int D2> friend class Buffer;

    /** Make a Buffer from another Buffer of possibly-different
     * dimensionality. Asserts if D is less than the dimensionality of
     * the argument. */
    template<int D2>
    Buffer(const Buffer<D2> &other) : buf(other.buf),
                                      dims(other.dims),
                                      ty(other.ty),
                                      alloc(other.alloc) {
        if (D < D2) {
            assert(other.dimensions() <= D);
        }
    }

    /** Move-construct a Buffer from another Buffer of
     * possibly-different dimensionality. Asserts if D is less than
     * the dimensionality of the argument. */
    template<int D2>
    Buffer(const Buffer<D2> &&other) : buf(other.buf),
                                       dims(other.dims),
                                       ty(other.ty),
                                       alloc(std::move(other.alloc)) {
        if (D < D2) {
            assert(other.dimensions() <= D);
        }
    }


    /** Assign from another Buffer of possibly-different
     * dimensionality. Asserts if D is less than the dimensionality of
     * the argument. */
    template<int D2>
    Buffer<D> &operator=(const Buffer<D2> &other) {
        if (D < D2) {
            assert(other.dimensions() <= D);
        }
        buf = other.buf;
        ty = other.ty;
        dims = other.dims;
        alloc = other.alloc;
        return *this;
    }

    /** Move from another Buffer of possibly-different
     * dimensionality. Asserts if D is less than the dimensionality of
     * the argument. */
    template<int D2>
    Buffer<D> &operator=(const Buffer<D2> &&other) {
        if (D < D2) {
            assert(other.dimensions() <= D);
        }
        buf = other.buf;
        ty = other.ty;
        dims = other.dims;
        alloc = std::move(other.alloc);
        return *this;
    }

    /** Allocate memory for this Image. Drops the reference to any
     * existing memory. */
    void allocate(void *(*allocate_fn)(size_t) = nullptr,
                  void (*deallocate_fn)(void *) = nullptr) {
        assert(buf.dev == 0);

        if (!allocate_fn) {
            allocate_fn = malloc;
        }
        if (!deallocate_fn) {
            deallocate_fn = free;
        }

        // Conservatively align images to 128 bytes. This is enough
        // alignment for all the platforms we might use.
        size_t size = size_in_bytes();
        const size_t alignment = 128;
        size = (size + alignment - 1) & ~(alignment - 1);
        uint8_t *ptr;
        ptr = (uint8_t *)allocate_fn(size + alignment - 1);
        alloc.reset(ptr, deallocate_fn);
        buf.host = (uint8_t *)((uintptr_t)(ptr + alignment - 1) & ~(alignment - 1));
    }

   /** Allocate a new image of the given size. Pass zeroes to make a
     * buffer suitable for bounds query calls. */
    template<typename ...Args>
    Buffer(halide_type_t t, int first, Args&&... rest) : ty(t) {
        static_assert(sizeof...(rest) < D,
                      "Too many arguments to constructor. Use Image<T, D>, where D is at least the desired number of dimensions");
        initialize_shape(0, first, int(rest)...);
        buf.elem_size = ty.bytes();
        dims = 1 + (int)(sizeof...(rest));
        if (!any_zero(first, int(rest)...)) {
            allocate();
        }
    }

    /** Make a Buffer that refers to a statically sized array. Does not
     * take ownership of the data. */
    template<typename Array, size_t N>
    explicit Buffer(Array (&vals)[N]) {
        dims = dimensionality_of_array(vals);
        initialize_shape_from_array_shape(dims - 1, vals);
        ty = scalar_type_of_array(vals);
        buf.elem_size = ty.bytes();
        buf.host = (uint8_t *)vals;
    }

    /** Initialize a Buffer from a pointer and some sizes. Assumes
     * dense row-major packing and a min coordinate of zero. Does not
     * take ownership of the data. */
    template<typename T, typename ...Args>
    explicit Buffer(T *data, int first, Args&&... rest) {
        static_assert(sizeof...(rest) < D,
                      "Too many arguments to constructor. Use Image<T, D>, where D is at least the desired number of dimensions");
        ty = halide_type_of<typename std::remove_cv<T>::type>();
        initialize_shape(0, first, int(rest)...);
        buf.elem_size = sizeof(T);
        dims = 1 + (int)(sizeof...(rest));
        buf.host = (uint8_t *)data;
    }

    /** Initialize an Image from a pointer to the min coordinate and
     * an array describing the shape.  Does not take ownership of the
     * data. */
    template<typename T, int N, typename std::enable_if<N < D>::type>
    explicit Buffer(T *data, halide_dimension_t shape[N]) {
        ty = halide_type_of<typename std::remove_cv<T>::type>();
        dims = N;
        for (int i = 0; i < N; i++) {
            buf.min[i]    = shape[i].min;
            buf.extent[i] = shape[i].extent;
            buf.stride[i] = shape[i].stride;
        }
        buf.elem_size = sizeof(T);
        buf.host = (uint8_t *)data;
    }

    template<typename T>
    explicit Buffer(T *data, halide_dimension_t shape[D]) {
        ty = halide_type_of<typename std::remove_cv<T>::type>();
        dims = 0;
        for (int i = 0; i < D; i++) {
            if (!shape[i].extent) break;
            dims++;
            buf.min[i]    = shape[i].min;
            buf.extent[i] = shape[i].extent;
            buf.stride[i] = shape[i].stride;
        }
        buf.elem_size = sizeof(T);
        buf.host = (uint8_t *)data;
    }

    /** If you use the (x, y, c) indexing convention, then Halide
     * Images are stored planar by default. This function constructs
     * an interleaved RGB or RGBA image that can still be indexed
     * using (x, y, c). Passing it to a generator requires that the
     * generator has been compiled with support for interleaved (also
     * known as packed or chunky) memory layouts. */
    static Buffer<D> make_interleaved(halide_type_t t, int width, int height, int channels) {
        static_assert(D >= 3, "Not enough dimensions to make an interleaved image");
        Buffer<D> im(t, channels, width, height);
        im.transpose(0, 1);
        im.transpose(1, 2);
        return im;
    }

    /** Get a pointer to the raw buffer_t this wraps. */
    // @{
    buffer_t *raw_buffer() {
        return &buf;
    }

    const buffer_t *raw_buffer() const {
        return &buf;
    }
    // @}

    /** Provide a cast operator to buffer_t *, so that instances can
     * be passed directly to Halide filters. */
    operator buffer_t *() {
        return &buf;
    }

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
    Buffer<D> copy(void *(*allocate_fn)(size_t) = nullptr,
                   void (*deallocate_fn)(void *) = nullptr) const {
        Buffer<D> src = *this;

        // Reorder the dimensions of src to have strides in increasing order
        int swaps[(D*(D+1))/2];
        int swaps_idx = 0;
        for (int i = dimensions()-1; i > 0; i--) {
            for (int j = i; j > 0; j--) {
                if (src.dim(j-1).stride() > src.dim(j).stride()) {
                    src.transpose(j-1, j);
                    swaps[swaps_idx++] = j;
                }
            }
        }

        // Make a copy of it using this dimension ordering
        Buffer<D> dst = src;
        dst.allocate(allocate_fn, deallocate_fn);

        // Concatenate dense inner dimensions into contiguous memcpy tasks
        Buffer<D> src_slice = src;
        Buffer<D> dst_slice = dst;
        int64_t slice_size = 1;
        while (src_slice.dimensions && src_slice.dim(0).stride() == slice_size) {
            assert(dst_slice.dim(0).stride() == slice_size);
            slice_size *= src_slice.dim(0).stride();
            src_slice = src_slice.sliced(0, src_slice.dim(0).min());
            dst_slice = dst_slice.sliced(0, dst_slice.dim(0).min());
        }

        slice_size *= buf.elem_size;
        // Do the memcpys
        src_slice.for_each_element([&](const int *pos) {
                memcpy(&dst_slice(pos), &src_slice(pos), slice_size);
            });

        // Undo the dimension reordering
        while (swaps_idx > 0) {
            int j = swaps[--swaps_idx];
            dst.transpose(j-1, j);
        }

        return dst;
    }

    /** Make an image that refers to a sub-range of this image along
     * the given dimension. Does not assert the crop region is within
     * the existing bounds. The cropped image drops any device
     * handle. */
    Buffer<D> cropped(int d, int min, int extent) const {
        // Make a fresh copy of the underlying buffer (but not a fresh
        // copy of the allocation, if there is one).
        Buffer<D> im = *this;
        // Drop the reference to any device allocation. It won't be
        // valid for the cropped image.
        im.buf.dev = 0;
        im.crop(d, min, extent);
        return im;
    }

    /** Crop an image in-place along the given dimension. */
    void crop(int d, int min, int extent) {
        // assert(dim(d).min() <= min);
        // assert(dim(d).max() >= min + extent - 1);
        int shift = min - dim(d).min();
        assert(buf.dev == 0 || shift == 0);
        buf.host += shift * dim(d).stride() * buf.elem_size;
        buf.min[d] = min;
        buf.extent[d] = extent;
    }

    /** Make an image that refers to a sub-rectangle of this image along
     * the first N dimensions. Does not assert the crop region is within
     * the existing bounds. The cropped image drops any device handle. */
    Buffer<D> cropped(const std::vector<std::pair<int, int>> &rect) const {
        // Make a fresh copy of the underlying buffer (but not a fresh
        // copy of the allocation, if there is one).
        Buffer<D> im = *this;
        // Drop the reference to any device allocation. It won't be
        // valid for the cropped image.
        im.buf.dev = 0;
        im.crop(rect);
        return im;
    }

    /** Crop an image in-place along the first N dimensions. */
    void crop(const std::vector<std::pair<int, int>> &rect) {
        for (int i = 0; i < rect.size(); i++) {
            crop(i, rect[i].first, rect[i].second);
        }
    }

    /** Make an image which refers to the same data with using
     * translated coordinates in the given dimension. Positive values
     * move the image data to the right or down relative to the
     * coordinate system. Drops any device handle. */
    Buffer<D> translated(int d, int dx) const {
        Buffer<D> im = *this;
        im.buf.dev = 0;
        im.translate(d, dx);
        return im;
    }

    /** Translate an image in-place along one dimension */
    void translate(int d, int delta) {
        buf.min[d] += delta;
    }

    /** Make an image which refers to the same data translated along
     * the first N dimensions. */
    void translated(const std::vector<int> &delta) {
        Buffer<D> im = *this;
        im.buf.dev = 0;
        im.translate(delta);
        return im;
    }

    /** Translate an image along the first N dimensions */
    void translate(const std::vector<int> &delta) {
        for (int i = 0; i < delta.size(); i++) {
            translate(i, delta[i]);
        }
    }

    /** Make an image which refers to the same data using a different
     * ordering of the dimensions. */
    Buffer<D> transposed(int d1, int d2) const {
        Buffer<D> im = *this;
        im.transpose(d1, d2);
        return im;
    }

    /** Transpose an image in-place */
    void transpose(int d1, int d2) {
        std::swap(buf.min[d1], buf.min[d2]);
        std::swap(buf.extent[d1], buf.extent[d2]);
        std::swap(buf.stride[d1], buf.stride[d2]);
    }

    /** Make a lower-dimensional image that refers to one slice of this
     * image. Drops any device handle. */
    Buffer<D-1> sliced(int d, int pos) const {
        Buffer<D> im = *this;
        im.buf.dev = 0;
        im.slice(d, pos);
        return Buffer<D-1>(std::move(im));
    }

    /** Slice an image in-place */
    void slice(int d, int pos) {
        // assert(pos >= dim(d).min() && pos <= dim(d).max());
        dims--;
        int shift = pos - dim(d).min();
        assert(buf.dev == 0 || shift == 0);
        buf.host += shift * dim(d).stride() * buf.elem_size;
        for (int i = d; i < dimensions(); i++) {
            buf.stride[i] = buf.stride[i+1];
            buf.extent[i] = buf.extent[i+1];
            buf.min[i] = buf.min[i+1];
        }
        buf.stride[dims] = buf.extent[dims] = buf.min[dims] = 0;
    }

    /** Make a new image that views this image as a single slice in a
     * higher-dimensional space. The new dimension has extent one and
     * the given min. Drops any device handle. This operation is the
     * opposite of slice. As an example, the following condition is
     * true:
     *
     \code
     im2 = im.embedded(1, 17);
     &im(x, y, c) == &im2(x, 17, y, c);
     \endcode
     */
    Buffer<D+1> embedded(int d, int pos) const {
        assert(d >= 0 && d <= dimensions());
        Buffer<D+1> im(*this);
        im.buf.dev = 0;
        im.add_dimension();
        im.translate(im.dimensions() - 1, pos);
        for (int i = im.dimensions(); i > d; i--) {
            im.transpose();
        }
        return im;
    }

    /** Embed an image in-place, increasing the
     * dimensionality. Requires that the actual number of dimensions
     * is less than template parameter D */
    void embed(int d, int pos) {
        assert(d >= 0 && d <= dimensions());
        add_dimension();
        translate(dimensions() - 1, pos);
        for (int i = dimensions() - 1; i > d; i--) {
            transpose(i, i-1);
        }
    }

    /** Add a new dimension with a min of zero and an extent of
     * one. The new dimension is the last dimension. This is a
     * special case of embed. It requires that the actual number of
     * dimensions is less than template parameter D. */
    void add_dimension() {
        // Check there's enough space for a new dimension.
        assert(dims < D);
        buf.min[dims] = 0;
        buf.extent[dims] = 1;
        if (dims == 0) {
            buf.stride[dims] = 1;
        } else {
            buf.stride[dims] = buf.extent[dims-1] * buf.stride[dims-1];
        }
        dims++;
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

/** A templated Image class that wraps buffer_t and adds
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
template<typename T, int D = 4>
class Image : public Buffer<D> {
    static_assert(D <= 4, "buffer_t supports a maximum of four dimensions");

public:
    typedef T ElemType;

    /** Get the type of the elements. Overridden here because we
     * statically know the type. */
    halide_type_t type() const {
        return halide_type_of<std::remove_cv<T>::type>();
    }

    Image() {}

    Image(const buffer_t &buf) : Buffer<D>(buf) {}

    /** Allocate a new image of the given size. Pass zeroes to make a
     * buffer suitable for bounds query calls. */
    template<typename ...Args>
    Image(int first, Args&&... rest) :
        Buffer<D>(halide_type_of<typename std::remove_cv<T>::type>(), first, int(rest)...) {}

    /** Make an image that refers to a statically sized array. Does not
     * take ownership of the data. */
    template<typename Array, size_t N>
    explicit Image(Array (&vals)[N]) :
        Buffer<D>(vals) {}

    /** Initialize an Image from a pointer and some sizes. Assumes
     * dense row-major packing and a min coordinate of zero. Does not
     * take ownership of the data. */
    template<typename ...Args>
    explicit Image(T *data, int first, Args&&... rest) :
        Buffer<D>(data, first, int(rest)...) {}

    /** Initialize an Image from a pointer to the min coordinate and
     * an array describing the shape.  Does not take ownership of the
     * data. */
    template<int N, typename std::enable_if<N < D>::type>
    explicit Image(T *data, halide_dimension_t shape[N]) : Buffer<D>(data, shape) {}

    /** Initialize an Image from a pointer to the min coordinate and
     * an array describing the shape.  Does not take ownership of the
     * data. This version exists so that there's a non-templated
     * version to use in case the Image is a derived type and so N
     * can't be inferred in the version above. */
    explicit Image(T *data, halide_dimension_t shape[D]) :
        Buffer<D>(data, shape) {}

    /** Construct a typed Image from an untyped Buffer. Asserts at
     * runtime if there's a type mismatch, or if the dimensionality of
     * the buffer is less than D. */
    template<int D2>
    Image(const Buffer<D2> &buf) : Buffer<D>(buf) {
        assert(halide_type_of<typename std::remove_cv<T>::type>() == buf.type());
    }

    /** Move-construct a typed Image from an untyped Buffer. Asserts
     * at runtime if there's a type mismatch, or if the dimensionality
     * of the buffer is less than D. */
    template<int D2>
    Image(const Buffer<D2> &&buf) : Buffer<D>(buf) {
        assert(halide_type_of<typename std::remove_cv<T>::type>() == buf.type());
    }

    /** Construct an Image from an Image of a different
     * dimensionality. Asserts at runtime the other dimensionality is
     * greater than D. Asserts at compile-time if the element type
     * doesn't match. This constructor is templated on the element
     * type of the argument so that the Buffer constructor above is
     * not used for Images with mismatched types.
     */
    template<typename T2, int D2>
    Image(const Image<T2, D2> &buf) : Buffer<D>(buf) {
        static_assert(std::is_same<typename std::remove_cv<T>::type, T2>::value,
                      "Can't construct an Image from an Image of different element type, "
                      "with the exception of casting an Image<T> to an Image<const T>.");
    }

    /** Move-construct an Image from an Image of a different
     * dimensionality. Asserts at runtime the other dimensionality is
     * greater than D. Asserts at compile-time if the element type
     * doesn't match.
     */
    template<typename T2, int D2>
    Image(const Image<T2, D2> &&buf) : Buffer<D>(buf) {
        static_assert(std::is_same<typename std::remove_cv<T>::type, T2>::value,
                      "Can't construct an Image from an Image of different element type, "
                      "with the exception of casting an Image<T> to an Image<const T>.");
    }

    /** Assign an Image from an Image of a different
     * dimensionality. Asserts at runtime the other dimensionality is
     * greater than D.
     */
    template<int D2>
    Image<T, D> &operator=(const Image<T, D2> &other) {
        Buffer<D>::operator=(other);
        return *this;
    }

    /** Move-assign an Image from an Image of a different
     * dimensionality. Asserts at runtime the other dimensionality is
     * greater than D.
     */
    template<int D2>
    Image<T, D> &operator=(const Image<T, D2> &&other) {
        Buffer<D>::operator=(other);
        return *this;
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
        im.transpose(0, 1);
        im.transpose(1, 2);
        return im;
    }

private:
    template<typename ...Args>
     __attribute__((always_inline))
    T *address_of(int d, int first, Args... rest) const {
        return address_of(d+1, rest...) + this->buf.stride[d] * (first - this->buf.min[d]);
    }

    __attribute__((always_inline))
    T *address_of(int d) const {
        return (T *)(this->buf.host);
    }

    __attribute__((always_inline))
    T *address_of(const int *pos) const {
        T *ptr = (T *)(this->buf.host);
        for (int i = this->dimensions() - 1; i >= 0; i--) {
            ptr += this->buf.stride[i] * (pos[i] - this->buf.min[i]);
        }
        return ptr;
    }
public:

    /** Get a pointer to the address of the min coordinate. */
    // @{
    T *data() {
        return (T *)(this->buf.host);
    }

    const T *data() const {
        return (const T *)(this->buf.host);
    }
    // @}

    /** Access elements. Use im(...) to get a reference to an element,
     * and use &im(...) to get the address of an element. If you pass
     * fewer arguments than the buffer has dimensions, the rest are
     * treated as their min coordinate.
     */
    //@{
    template<typename ...Args>
    __attribute__((always_inline))
    const T &operator()(int first, Args... rest) const {
        return *(address_of(0, first, int(rest)...));
    }

    __attribute__((always_inline))
    const T &operator()() const {
        return *(address_of(0));
    }

    __attribute__((always_inline))
    const T &operator()(const int *pos) const {
        return *((T *)address_of(pos));
    }

    template<typename ...Args>
    __attribute__((always_inline))
    T &operator()(int first, Args... rest) {
        return *(address_of(0, first, int(rest)...));
    }

    __attribute__((always_inline))
    T &operator()() {
        return *(address_of(0));
    }

    __attribute__((always_inline))
    T &operator()(const int *pos) {
        return *((T *)address_of(pos));
    }
    // @}

private:
    // Helper functions for fill that call for_each_element with a
    // lambda of the correct dimensionality.
    template<typename ...Args>
    typename std::enable_if<(sizeof...(Args) < D)>::type
    fill_helper(T val, Args... args) {
        if (sizeof...(Args) == Buffer<D>::dimensions()) {
            Buffer<D>::for_each_element([&](Args... args) {(*this)(args...) = val;});
        } else {
            fill_helper(val, 0, args...);
        }
    }

    template<typename ...Args>
    typename std::enable_if<(sizeof...(Args) == D)>::type
    fill_helper(T val, Args...) {
        Buffer<D>::for_each_element([&](Args... args) {(*this)(args...) = val;});
    }

public:

    /** Set every value in the buffer to the given value */
    void fill(T val) {
        fill_helper(val);
    }

    /** Make a new image which is a deep copy of this image. Use crop
     * or slice followed by copy to make a copy of only a portion of
     * the image. The new image uses the same memory layout as the
     * original, with holes compacted away. */
    Image<T, D> copy(void *(*allocate_fn)(size_t) = nullptr,
                     void (*deallocate_fn)(void *) = nullptr) const {
        return Image<T, D>(Buffer<D>::copy(allocate_fn, deallocate_fn));
    }

    /** Make an image that refers to a sub-range of this image along
     * the given dimension. Does not assert the crop region is within
     * the existing bounds. */
    Image<T, D> cropped(int d, int min, int extent) const {
        return Image<T, D>(Buffer<D>::cropped(d, min, extent));
    }

    /** Make an image that refers to a sub-rectangle of this image along
     * the first N dimensions. Does not assert the crop region is within
     * the existing bounds. The cropped image drops any device handle. */
    Image<T, D> cropped(const std::vector<std::pair<int, int>> &rect) const {
        return Image<T, D>(Buffer<D>::cropped(rect));
    }

    /** Make an image which refers to the same data with using
     * translated coordinates in the given dimension. Positive values
     * move the image data to the right or down relative to the
     * coordinate system. */
    Image<T, D> translated(int d, int dx) const {
        return Image<T, D>(Buffer<D>::translated(d, dx));
    }

    /** Make an image which refers to the same data with using
     * translated coordinates along the first N dimensions. Positive
     * values move the image data to the right or down relative to the
     * coordinate system. */
    Image<T, D> translated(const std::vector<int> &delta) const {
        return Image<T, D>(Buffer<D>::translated(delta));
    }

    /** Make an image which refers to the same data using a different
     * ordering of the dimensions. */
    Image<T, D> transposed(int d1, int d2) const {
        return Image<T, D>(Buffer<D>::transposed(d1, d2));
    }

    /** Make a lower-dimensional image that refers to one slice of this
     * image. */
    Image<T, D-1> sliced(int d, int pos) const {
        return Image<T, D-1>(Buffer<D>::sliced(d, pos));
    }

    /** Make a higher-dimensional image in which this image is one
     * slice. The opposite of sliced. */
    Image<T, D+1> embedded(int d, int pos) const {
        return Image<T, D+1>(Buffer<D>::embedded(d, pos));
    }

};

/** Some helpers for for_each_element. */
template<typename Fn>
struct for_each_element_helpers {

    /** If f is callable with this many args, call it. The first dummy
     * argument is to make this version preferable for overload
     * resolution. The decltype is to make this version impossible if
     * the function is not callable with this many args. */
    template<typename ...Args>
    __attribute__((always_inline))
    static auto for_each_element_variadic(int, int d, Fn &&f, const buffer_t &buf, Args... args)
        -> decltype(f(args...)) {
        f(args...);
    }

    /** If the above overload is impossible, we add an outer loop over
     * an additional argument and try again. This trick is known as
     * SFINAE. */
    template<typename ...Args>
    __attribute__((always_inline))
    static void for_each_element_variadic(double, int d, Fn &&f, const buffer_t &buf, Args... args) {
        int e = buf.extent[d] == 0 ? 1 : buf.extent[d];
        for (int i = 0; i < e; i++) {
            for_each_element_variadic(0, d-1, std::forward<Fn>(f), buf, buf.min[d] + i, args...);
        }
    }

    /** A sink function used to suppress compiler warnings in
     * compilers that don't think decltype counts as a use. */
    template<typename ...Args>
    static void sink(Args... ) {}

    /** Determine the minimum number of arguments a callable can take
     * using the same trick. */
    template<typename ...Args>
    __attribute__((always_inline))
    static auto num_args(int, int *result, Fn &&f, Args... args) -> decltype(f(args...)) {
        *result = sizeof...(args);
        sink(std::forward<Fn>(f), args...);
    }

    /** The recursive version is only enabled up to a recursion limit
     * of 256. This catches callables that aren't callable with any
     * number of ints. */
    template<typename ...Args>
    __attribute__((always_inline))
    static void num_args(double, int *result, Fn &&f, Args... args) {
        static_assert(sizeof...(args) <= 256,
                      "Callable passed to for_each_element must accept either a const int *,"
                      " or up to 256 ints. No such operator found. Expect infinite template recursion.");
        return num_args(0, result, std::forward<Fn>(f), 0, args...);
    }

    __attribute__((always_inline))
    static int get_number_of_args(Fn &&f) {
        int result;
        num_args(0, &result, std::forward<Fn>(f));
        return result;
    }

    /** A version where the callable takes a position array instead,
     * with compile-time recursion on the dimensionality.  This
     * overload is preferred to the one below using the same int vs
     * double trick as above, but is impossible once d hits -1 using
     * std::enable_if. */
    template<int d>
    __attribute__((always_inline))
    static typename std::enable_if<d >= 0, void>::type
    for_each_element_array_helper(int, Fn &&f, const buffer_t &buf, int *pos) {
        for (pos[d] = buf.min[d]; pos[d] < buf.min[d] + buf.extent[d]; pos[d]++) {
            for_each_element_array_helper<d - 1>(0, std::forward<Fn>(f), buf, pos);
        }
    }

    /** Base case for recursion above. */
    template<int d>
    __attribute__((always_inline))
    static void for_each_element_array_helper(double, Fn &&f, const buffer_t &buf, int *pos) {
        f(pos);
    }


    /** A run-time-recursive version (instead of
     * compile-time-recursive) that requires the callable to take a
     * pointer to a position array instead. Dispatches to the
     * compile-time-recursive version once the dimensionality gets
     * small. */
    static void for_each_element_array(int d, Fn &&f, const buffer_t &buf, int *pos) {
        if (d == -1) {
            f(pos);
        } else if (d == 0) {
            // Once the dimensionality gets small enough, dispatch to
            // a compile-time-recursive version for better codegen of
            // the inner loops.
            for_each_element_array_helper<0>(0, std::forward<Fn>(f), buf, pos);
        } else if (d == 1) {
            for_each_element_array_helper<1>(0, std::forward<Fn>(f), buf, pos);
        } else if (d == 2) {
            for_each_element_array_helper<2>(0, std::forward<Fn>(f), buf, pos);
        } else if (d == 3) {
            for_each_element_array_helper<3>(0, std::forward<Fn>(f), buf, pos);
        } else {
            for (pos[d] = buf.min[d]; pos[d] < buf.min[d] + buf.extent[d]; pos[d]++) {
                for_each_element_array(d - 1, std::forward<Fn>(f), buf, pos);
            }
        }
    }

    /** We now have two overloads for for_each_element. This one
     * triggers if the callable takes a const int *.
     */
    template<typename Fn2>
    static auto for_each_element(int, const buffer_t &buf, Fn2 &&f)
        -> decltype(f((const int *)0)) {
        int pos[4] = {0, 0, 0, 0};
        int dimensions = 0;
        while (buf.extent[dimensions] != 0 && dimensions < 4) {
            dimensions++;
        }
        for_each_element_array(dimensions - 1, std::forward<Fn2>(f), buf, pos);
    }

    /** This one triggers otherwise. It treats the callable as
     * something that takes some number of ints. */
    template<typename Fn2>
    __attribute__((always_inline))
    static void for_each_element(double, const buffer_t &buf, Fn2 &&f) {
        int num_args = get_number_of_args(std::forward<Fn2>(f));
        for_each_element_variadic(0, num_args-1, std::forward<Fn2>(f), buf);
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
void for_each_element(const buffer_t &buf, Fn &&f) {
    for_each_element_helpers<Fn>::for_each_element(0, buf, std::forward<Fn>(f));
}

}  // namespace Tools
}  // namespace Halide

#endif  // HALIDE_RUNTIME_IMAGE_H

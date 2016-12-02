/** \file
 * Defines a Buffer type that wraps from buffer_t and adds
 * functionality, and methods for more conveniently iterating over the
 * samples in a buffer_t outside of Halide code. */

#ifndef HALIDE_RUNTIME_BUFFER_H
#define HALIDE_RUNTIME_BUFFER_H

#include <memory>
#include <vector>
#include <cassert>
#include <atomic>
#include <stdint.h>
#include <string.h>

#include "HalideRuntime.h"

// Certain methods of Buffer *must* be inlined for reasonable
// performance in debug builds.
#ifdef _MSC_VER
#define ALWAYS_INLINE __forceinline
#else
#define ALWAYS_INLINE __attribute__((always_inline))
#endif

// gcc 5.1 has a false positive warning on this code
#if __GNUC__ == 5 && __GNUC_MINOR__ == 1
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

/** A C struct describing the shape of a single dimension of a halide
 * buffer. This will be a type in the runtime once halide_buffer_t is
 * merged. */
struct halide_dimension_t {
    int min, extent, stride;
};

namespace Halide {

template<typename Fn>
void for_each_element(const buffer_t &buf, Fn &&f);

// Forward-declare our Buffer class
template<typename T, int D> class Buffer;

// This declaration exists so that Buffer is extensible with custom
// operator()(Args...) methods. Add implementations of it for whatever
// types you like. Use enable_if if necessary to stop the overloads
// being ambiguous.
template<typename Ret, typename T, int D, typename ...Args>
Ret image_accessor(const Buffer<T, D> &, Args...);

// A helper to check if a parameter pack is entirely implicitly
// int-convertible to use with std::enable_if
template<typename ...Args>
struct AllInts {
    static const bool value = false;
};

template<>
struct AllInts<> {
    static const bool value = true;
};

template<typename T, typename ...Args>
struct AllInts<T, Args...> {
    static const bool value = std::is_convertible<T, int>::value && AllInts<Args...>::value;
};

// Floats and doubles are technically implicitly int-convertible, but
// doing so produces a warning we treat as an error, so just disallow
// it here.
template<typename ...Args>
struct AllInts<float, Args...> {
    static const bool value = false;
};

template<typename ...Args>
struct AllInts<double, Args...> {
    static const bool value = false;
};

/** A struct acting as a header for allocations owned by the Buffer
 * class itself. */
struct AllocationHeader {
    void (*deallocate_fn)(void *);
    std::atomic<int> ref_count;
};

/** A templated Buffer class that wraps buffer_t and adds
 * functionality. When using Halide from C++, this is the preferred
 * way to create input and output buffers. The overhead of using this
 * class relative to a naked buffer_t is minimal - it uses another
 * ~100 bytes on the stack, and does no dynamic allocations when using
 * it to represent existing memory. This overhead will shrink further
 * in the future once buffer_t is deprecated.
 *
 * The template parameter T is the element type, and D is the maximum
 * number of dimensions. It must be less than or equal to 4 for
 * now. For buffers where the element type is not known at compile
 * time, use void for T.
 *
 * The class optionally allocates and owns memory for the image using
 * a shared pointer allocated with the provided allocator. If they are
 * null, malloc and free are used.  Any device-side allocation is
 * considered as owned if and only if the host-side allocation is
 * owned.
 *
 * For accessing the shape and type, this class provides both the
 * buffer_t-style interface (extent(i), min(i), and stride(i)), and
 * also the interface of the yet-to-come halide_buffer_t, which will
 * replace buffer_t. This is intended to allow a gradual transition to
 * halide_buffer_t. New code should access the shape via
 * dim(i).extent(), dim(i).min(), and dim(i).stride() */
template<typename T = void, int D = 4>
class Buffer {
    static_assert(D <= 4, "buffer_t supports a maximum of four dimensions");

    /** The underlying buffer_t */
    buffer_t buf = {0};

    /** The dimensionality of the buffer */
    int dims = 0;

    /** The type of the elements */
    halide_type_t ty;

    /** The allocation owned by this Buffer. NULL if the Buffer does not
     * own the memory. */
    AllocationHeader *alloc = nullptr;

    /** A reference count for the device allocation owned by this buffer. */
    mutable std::atomic<int> *dev_ref_count = nullptr;

    /** True if T is of type void or const void */
    static const bool T_is_void = std::is_same<typename std::remove_const<T>::type, void>::value;

    /** A type function that adds a const qualifier if T is a const type. */
    template<typename T2>
    using add_const_if_T_is_const = typename std::conditional<std::is_const<T>::value, const T2, T2>::type;

    /** T unless T is (const) void, in which case (const)
     * uint8_t. Useful for providing return types for operator() */
    using not_void_T = typename std::conditional<T_is_void,
                                                 add_const_if_T_is_const<uint8_t>,
                                                 T>::type;

    /** The type the elements are stored as. Equal to not_void_T
     * unless T is a pointer, in which case uint64_t. Halide stores
     * all pointer types as uint64s internally, even on 32-bit
     * systems. */
    using storage_T = typename std::conditional<std::is_pointer<T>::value, uint64_t, not_void_T>::type;

public:
    /** Return true if the Halide type is not void (or const void). */
    static bool has_static_halide_type() {
        return !T_is_void;
    }

    /** Get the Halide type of T. Callers should not use the result if
     * has_static_halide_type() returns false. */
    static halide_type_t static_halide_type() {
        // assert(has_static_halide_type());  TODO
        return halide_type_of<typename std::remove_cv<not_void_T>::type>();
    }

    /** Is this Buffer responsible for managing its own memory */
    bool manages_memory() const {
        return alloc != nullptr;
    }
    
    /** Return the maximum number of dimensions for this Buffer type. */
    static int static_halide_max_dimensions() {
        return D;
    }

private:
    /** Increment the reference count of any owned allocation */
    void incref() const {
        if (!manages_memory()) return;
        if (alloc) {
            alloc->ref_count++;
        }
        if (buf.dev) {
            if (!dev_ref_count) {
                // I seem to have a non-zero dev field but no
                // reference count for it. I must have been given a
                // device allocation by a Halide pipeline, and have
                // never been copied from since. Take sole ownership
                // of it.
                dev_ref_count = new std::atomic<int>(1);
            }
            (*dev_ref_count)++;
        }
    }

    /** Decrement the reference count of any owned allocation and free host
     * and device memory if it hits zero. Sets alloc to nullptr. */
    void decref() {
        if (!manages_memory()) return;
        if (alloc) {
            int new_count = --(alloc->ref_count);
            if (new_count == 0) {
                void (*fn)(void *) = alloc->deallocate_fn;
                fn(alloc);
            }
        }
        decref_dev();
        buf.host = nullptr;
        alloc = nullptr;
    }

    void decref_dev() {
        if (!manages_memory() || buf.dev == 0) return;
        int new_count = 0;
        if (dev_ref_count) {
            new_count = --(*dev_ref_count);
        }
        if (new_count == 0) {
            halide_device_free_t fn = halide_get_device_free_fn();
            assert(fn && "Buffer has a device allocation but no Halide Runtime linked");
            (*fn)(nullptr, &buf);
            if (dev_ref_count) {
                delete dev_ref_count;
                dev_ref_count = nullptr;
            }
        }
        buf.dev = 0;
    }

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

    /** Initialize the shape from a vector of extents */
    void initialize_shape(const std::vector<int> &sizes) {
        for (size_t i = 0; i < sizes.size(); i++) {
            buf.min[i] = 0;
            buf.extent[i] = sizes[i];
            if (i == 0) {
                buf.stride[i] = 1;
            } else {
                buf.stride[i] = buf.stride[i-1] * buf.extent[i-1];
            }
        }
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
    template<typename T2>
    void initialize_shape_from_array_shape(int, const T2 &) {
    }

    /** Get the dimensionality of a multi-dimensional C array */
    template<typename Array, size_t N>
    static int dimensionality_of_array(Array (&vals)[N]) {
        return dimensionality_of_array(vals[0]) + 1;
    }

    template<typename T2>
    static int dimensionality_of_array(const T2 &) {
        return 0;
    }

    /** Get the underlying halide_type_t of an array's element type. */
    template<typename Array, size_t N>
    static halide_type_t scalar_type_of_array(Array (&vals)[N]) {
        return scalar_type_of_array(vals[0]);
    }

    template<typename T2>
    static halide_type_t scalar_type_of_array(const T2 &) {
        return halide_type_of<typename std::remove_cv<T2>::type>();
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

    static bool any_zero(const std::vector<int> &v) {
        for (int i : v) {
            if (i == 0) return true;
        }
        return false;
    }

public:

    typedef T ElemType;

    /** Read-only access to the shape */
    class Dimension {
        const buffer_t &buf;
        const int idx;
    public:
        /** The lowest coordinate in this dimension */
        ALWAYS_INLINE int min() const {
            return buf.min[idx];
        }

        /** The number of elements in memory you have to step over to
         * increment this coordinate by one. */
        ALWAYS_INLINE int stride() const {
            return buf.stride[idx];
        }

        /** The extent of the image along this dimension */
        ALWAYS_INLINE int extent() const {
            return buf.extent[idx];
        }

        /** The highest coordinate in this dimension */
        ALWAYS_INLINE int max() const {
            return min() + extent() - 1;
        }

        /** An iterator class, so that you can iterate over
         * coordinates in a dimensions using a range-based for loop. */
        struct iterator {
            int val;
            int operator*() const {return val;}
            bool operator!=(const iterator &other) const {return val != other.val;}
            iterator &operator++() {val++; return *this;}
        };

        /** An iterator that points to the min coordinate */
        ALWAYS_INLINE iterator begin() const {
            return {min()};
        }

        /** An iterator that points to one past the max coordinate */
        ALWAYS_INLINE iterator end() const {
            return {min() + extent()};
        }

        Dimension(const buffer_t &buf, int idx) : buf(buf), idx(idx) {}
    };

    /** Access the shape of the buffer */
    ALWAYS_INLINE Dimension dim(int i) const {
        return Dimension(buf, i);
    }

    /** Access to the mins, strides, extents. Will be deprecated. Do not use. */
    // @{
    int min(int i) const { return dim(i).min(); }
    int extent(int i) const { return dim(i).extent(); }
    int stride(int i) const { return dim(i).stride(); }
    // @}

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
    T *begin() const {
        ptrdiff_t index = 0;
        for (int i = 0; i < dimensions(); i++) {
            if (dim(i).stride() < 0) {
                index += dim(i).stride() * (dim(i).extent() - 1);
            }
        }
        return (T *)(buf.host + index * buf.elem_size);
    }

    /** A pointer to one beyond the element with the highest address. */
    T *end() const {
        ptrdiff_t index = 0;
        for (int i = 0; i < dimensions(); i++) {
            if (dim(i).stride() > 0) {
                index += dim(i).stride() * (dim(i).extent() - 1);
            }
        }
        index += 1;
        return (T *)(buf.host + index * buf.elem_size);
    }

    /** The total number of bytes spanned by the data in memory. */
    size_t size_in_bytes() const {
        return (size_t)((uint8_t *)end() - (uint8_t *)begin());
    }

    Buffer() : ty(static_halide_type()) {}

    /** Make a buffer from a buffer_t */
    Buffer(const buffer_t &buf) : ty(static_halide_type()) {
        static_assert(!T_is_void, "Can't construct an Buffer<void> from a buffer_t. Type is unknown.");
        initialize_from_buffer(buf);
    }

    Buffer(halide_type_t t, const buffer_t &buf) : ty(t) {
        initialize_from_buffer(buf);
    }

    /** Give Buffers access to the members of Buffers of different dimensionalities and types. */
    template<typename T2, int D2> friend class Buffer;

    /** Fail an assertion at runtime or compile-time if an Buffer<T, D>
     * cannot be constructed from some other Buffer type. */
    template<typename T2, int D2>
    static void assert_can_convert_from(const Buffer<T2, D2> &other) {
        static_assert((!std::is_const<T2>::value || std::is_const<T>::value),
                      "Can't convert from a Buffer<const T> to a Buffer<T>");
        static_assert(std::is_same<typename std::remove_const<T>::type,
                                   typename std::remove_const<T2>::type>::value ||
                      T_is_void || Buffer<T2, D2>::T_is_void,
                      "type mismatch constructing Buffer");
        if (D < D2) {
            assert(other.dimensions() <= D);
        }
        if (Buffer<T2, D2>::T_is_void && !T_is_void) {
            assert(other.ty == static_halide_type());
        }
    }

    /** Make a Buffer<T> from another Buffer<T> of possibly-different
     * dimensionality and type. Asserts if D is less than the
     * dimensionality of the argument, or if there's a type
     * mismatch. */
    template<typename T2, int D2>
    Buffer(const Buffer<T2, D2> &other) : buf(other.buf),
                                          dims(other.dims),
                                          ty(other.ty),
                                          alloc(other.alloc) {
        assert_can_convert_from(other);
        other.incref();
        dev_ref_count = other.dev_ref_count;
    }

    Buffer(const Buffer<T, D> &other) : buf(other.buf),
                                        dims(other.dims),
                                        ty(other.ty),
                                        alloc(other.alloc) {
        other.incref();
        dev_ref_count = other.dev_ref_count;
    }

    /** Move-construct an Buffer from another Buffer of
     * possibly-different dimensionality. Asserts if D is less than
     * the dimensionality of the argument, or if there's a type
     * mismatch. */
    template<typename T2, int D2>
    Buffer(Buffer<T2, D2> &&other) : buf(other.buf),
                                     dims(other.dims),
                                     ty(other.ty),
                                     alloc(other.alloc),
                                     dev_ref_count(other.dev_ref_count) {
        assert_can_convert_from(other);
        other.dev_ref_count = nullptr;
        other.alloc = nullptr;
    }

    Buffer(Buffer<T, D> &&other) : buf(other.buf),
                                   dims(other.dims),
                                   ty(other.ty),
                                   alloc(other.alloc),
                                   dev_ref_count(other.dev_ref_count) {
        other.dev_ref_count = nullptr;
        other.alloc = nullptr;
    }

    /** Assign from another Buffer of possibly-different dimensionality
     * and type. Asserts if D is less than the dimensionality of the
     * argument, or if there's a type mismatch. */
    template<typename T2, int D2>
    Buffer<T, D> &operator=(const Buffer<T2, D2> &other) {
        assert_can_convert_from(other);
        other.incref();
        decref();
        dev_ref_count = other.dev_ref_count;
        alloc = other.alloc;
        ty = other.ty;
        dims = other.dims;
        buf = other.buf;
        return *this;
    }

    Buffer<T, D> &operator=(const Buffer<T, D> &other) {
        other.incref();
        decref();
        dev_ref_count = other.dev_ref_count;
        alloc = other.alloc;
        buf = other.buf;
        ty = other.ty;
        dims = other.dims;
        return *this;
    }

    /** Move from another Buffer of possibly-different dimensionality
     * and type. Asserts if D is less than the dimensionality of the
     * argument, or if there's a type mismatch. */
    template<typename T2, int D2>
    Buffer<T, D> &operator=(Buffer<T2, D2> &&other) {
        assert_can_convert_from(other);
        std::swap(alloc, other.alloc);
        std::swap(dev_ref_count, other.dev_ref_count);
        buf = other.buf;
        ty = other.ty;
        dims = other.dims;
        return *this;
    }

    Buffer<T, D> &operator=(Buffer<T, D> &&other) {
        std::swap(alloc, other.alloc);
        std::swap(dev_ref_count, other.dev_ref_count);
        buf = other.buf;
        ty = other.ty;
        dims = other.dims;
        return *this;
    }

    /** Check the product of the extents fits in memory. */
    void check_overflow() {
        size_t size = ty.bytes();
        for (int i = 0; i < dimensions(); i++) {
            size *= dim(i).extent();
        }
        // We allow 2^31 or 2^63 bytes, so drop the top bit.
        size = (size << 1) >> 1;
        for (int i = 0; i < dimensions(); i++) {
            size /= dim(i).extent();
        }
        assert(size == ty.bytes() && "Error: Overflow computing total size of buffer.");
    }

    /** Allocate memory for this Buffer. Drops the reference to any
     * owned memory. */
    void allocate(void *(*allocate_fn)(size_t) = nullptr,
                  void (*deallocate_fn)(void *) = nullptr) {
        if (!allocate_fn) {
            allocate_fn = malloc;
        }
        if (!deallocate_fn) {
            deallocate_fn = free;
        }

        // Drop any existing allocation
        deallocate();

        // Conservatively align images to 128 bytes. This is enough
        // alignment for all the platforms we might use.
        size_t size = size_in_bytes();
        const size_t alignment = 128;
        size = (size + alignment - 1) & ~(alignment - 1);
        alloc = (AllocationHeader *)allocate_fn(size + sizeof(AllocationHeader) + alignment - 1);
        alloc->deallocate_fn = deallocate_fn;
        alloc->ref_count = 1;
        uint8_t *unaligned_ptr = ((uint8_t *)alloc) + sizeof(AllocationHeader);
        buf.host = (uint8_t *)((uintptr_t)(unaligned_ptr + alignment - 1) & ~(alignment - 1));
    }

    /** Drop reference to any owned memory, possibly freeing it, if
     * this buffer held the last reference to it. Retains the shape of
     * the buffer. Does nothing if this buffer did not allocate its
     * own memory. */
    void deallocate() {
        decref();
    }

    /** Drop reference to any owned device memory, possibly freeing it
     * if this buffer held the last reference to it. Does nothing if
     * this buffer did not allocate its own memory. */
    void device_deallocate() {
        decref_dev();
    }
    
    /** Allocate a new image of the given size with a runtime
     * type. Only used when you do know what size you want but you
     * don't know statically what type the elements are. Pass zeroes
     * to make a buffer suitable for bounds query calls. */
    template<typename ...Args,
             typename = typename std::enable_if<AllInts<Args...>::value>::type>
    Buffer(halide_type_t t, int first, Args... rest) : ty(t) {
        if (!T_is_void) {
            assert(static_halide_type() == t);
        }
        static_assert(sizeof...(rest) < D,
                      "Too many arguments to constructor. Use Buffer<T, D>, "
                      "where D is at least the desired number of dimensions");
        initialize_shape(0, first, rest...);
        buf.elem_size = ty.bytes();
        dims = 1 + (int)(sizeof...(rest));
        if (!any_zero(first, rest...)) {
            check_overflow();
            allocate();
        }
    }


    /** Allocate a new image of the given size. Pass zeroes to make a
     * buffer suitable for bounds query calls. */
    template<typename ...Args,
             typename = typename std::enable_if<AllInts<Args...>::value>::type>
    Buffer(int first, Args... rest) : ty(static_halide_type()) {
        static_assert(!T_is_void,
                      "To construct an Buffer<void>, pass a halide_type_t as the first argument to the constructor");
        static_assert(sizeof...(rest) < D,
                      "Too many arguments to constructor. Use Buffer<T, D>, "
                      "where D is at least the desired number of dimensions");
        initialize_shape(0, first, rest...);
        buf.elem_size = ty.bytes();
        dims = 1 + (int)(sizeof...(rest));
        if (!any_zero(first, rest...)) {
            check_overflow();
            allocate();
        }
    }

    /** Allocate a new image of unknown type using a vector of ints as the size. */
    Buffer(halide_type_t t, const std::vector<int> &sizes) : ty(t) {
        if (!T_is_void) {
            assert(static_halide_type() == t);
        }
        assert(sizes.size() <= D);
        initialize_shape(sizes);
        buf.elem_size = ty.bytes();
        dims = (int)sizes.size();
        if (!any_zero(sizes)) {
            check_overflow();
            allocate();
        }
    }

    /** Make an Buffer that refers to a statically sized array. Does not
     * take ownership of the data, and does not set the host_dirty flag. */
    template<typename Array, size_t N>
    explicit Buffer(Array (&vals)[N]) {
        dims = dimensionality_of_array(vals);
        initialize_shape_from_array_shape(dims - 1, vals);
        ty = scalar_type_of_array(vals);
        buf.elem_size = ty.bytes();
        buf.host = (uint8_t *)vals;
    }

    /** Initialize an Buffer of runtime type from a pointer and some
     * sizes. Assumes dense row-major packing and a min coordinate of
     * zero. Does not take ownership of the data and does not set the
     * host_dirty flag. */
    template<typename ...Args,
             typename = typename std::enable_if<AllInts<Args...>::value>::type>
    explicit Buffer(halide_type_t t, add_const_if_T_is_const<void> *data, int first, Args&&... rest) {
        if (!T_is_void) {
            assert(static_halide_type() == t);
        }
        static_assert(sizeof...(rest) < D,
                      "Too many arguments to constructor. Use Buffer<T, D>, "
                      "where D is at least the desired number of dimensions");
        ty = t;
        initialize_shape(0, first, int(rest)...);
        buf.elem_size = ty.bytes();
        dims = 1 + (int)(sizeof...(rest));
        buf.host = (uint8_t *)data;
    }

    /** Initialize an Buffer from a pointer and some sizes. Assumes
     * dense row-major packing and a min coordinate of zero. Does not
     * take ownership of the data and does not set the host_dirty flag. */
    template<typename ...Args,
             typename = typename std::enable_if<AllInts<Args...>::value>::type>
    explicit Buffer(T *data, int first, Args&&... rest) {
        static_assert(sizeof...(rest) < D,
                      "Too many arguments to constructor. Use Buffer<T, D>, "
                      "where D is at least the desired number of dimensions");
        ty = static_halide_type();
        initialize_shape(0, first, int(rest)...);
        buf.elem_size = ty.bytes();
        dims = 1 + (int)(sizeof...(rest));
        buf.host = (uint8_t *)data;
    }

    /** Initialize an Buffer from a pointer and a vector of
     * sizes. Assumes dense row-major packing and a min coordinate of
     * zero. Does not take ownership of the data and does not set the
     * host_dirty flag. */
    explicit Buffer(T *data, const std::vector<int> &sizes) {
        assert(sizes.size() <= D);
        initialize_shape(sizes);
        ty = static_halide_type();
        buf.elem_size = ty.bytes();
        dims = (int)sizes.size();
        buf.host = (uint8_t *)data;
    }

    /** Initialize an Buffer of runtime type from a pointer and a
     * vector of sizes. Assumes dense row-major packing and a min
     * coordinate of zero. Does not take ownership of the data and
     * does not set the host_dirty flag. */
    explicit Buffer(halide_type_t t, add_const_if_T_is_const<void> *data, const std::vector<int> &sizes) {
        if (!T_is_void) {
            assert(static_halide_type() == t);
        }
        assert(sizes.size() <= D);
        initialize_shape(sizes);
        ty = t;
        buf.elem_size = ty.bytes();
        dims = (int)sizes.size();
        buf.host = (uint8_t *)data;
    }

    /** Initialize an Buffer from a pointer to the min coordinate and
     * an array describing the shape.  Does not take ownership of the
     * data, and does not set the host_dirty flag. */
    explicit Buffer(halide_type_t t, add_const_if_T_is_const<void> *data, int d, const halide_dimension_t *shape) {
        if (!T_is_void) {
            assert(static_halide_type() == t);
        }
        ty = t;
        dims = d;
        for (int i = 0; i < d; i++) {
            buf.min[i]    = shape[i].min;
            buf.extent[i] = shape[i].extent;
            buf.stride[i] = shape[i].stride;
        }
        buf.elem_size = ty.bytes();
        buf.host = (uint8_t *)data;
    }

    /** Initialize an Buffer from a pointer to the min coordinate and
     * an array describing the shape.  Does not take ownership of the
     * data and does not set the host_dirty flag. */
    explicit Buffer(T *data, int d, const halide_dimension_t *shape) {
        ty = halide_type_of<typename std::remove_cv<T>::type>();
        dims = d;
        for (int i = 0; i < d; i++) {
            buf.min[i]    = shape[i].min;
            buf.extent[i] = shape[i].extent;
            buf.stride[i] = shape[i].stride;
        }
        buf.elem_size = ty.bytes();
        buf.host = (uint8_t *)data;
    }

    /** Destructor. Will release any underlying owned allocation if
     * this is the last reference to it. */
    ~Buffer() {
        decref();
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

    /** Access to the untyped host pointer */
    // @{
    const void *host_ptr() const {
        return buf.host;
    }
    void *host_ptr() {
        return buf.host;
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

    /** Conventional names for the min and max value of each dimension */
    // @{
    int left() const {
        return dim(0).min();
    }

    int right() const {
        return dim(0).max();
    }

    int top() const {
        return dim(1).min();
    }

    int bottom() const {
        return dim(1).max();
    }
    // @}

    /** Make a new image which is a deep copy of this image using the
     * minimum number of calls to memcpy. Use crop or slice followed
     * by copy to make a copy of only a portion of the image. The new
     * image uses the same memory layout as the original, with holes
     * compacted away. */
    Buffer<T, D> copy(void *(*allocate_fn)(size_t) = nullptr,
                     void (*deallocate_fn)(void *) = nullptr) const {
        Buffer<T, D> src = *this;

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
        Buffer<T, D> dst = src;
        dst.allocate(allocate_fn, deallocate_fn);

        // Concatenate dense inner dimensions into contiguous memcpy tasks
        Buffer<T, D> src_slice = src;
        Buffer<T, D> dst_slice = dst;
        int64_t slice_size = 1;
        while (src_slice.dimensions() && src_slice.dim(0).stride() == slice_size) {
            assert(dst_slice.dim(0).stride() == slice_size);
            slice_size *= src_slice.dim(0).extent();
            src_slice = src_slice.sliced(0, src_slice.dim(0).min());
            dst_slice = dst_slice.sliced(0, dst_slice.dim(0).min());
        }

        slice_size *= buf.elem_size;
        // Do the memcpys
        src_slice.for_each_element([&](const int *pos) {
            memcpy(dst_slice.address_of(pos), src_slice.address_of(pos), slice_size);
        });

        // Undo the dimension reordering
        while (swaps_idx > 0) {
            int j = swaps[--swaps_idx];
            dst.transpose(j-1, j);
        }

        return dst;
    }

    /** Fill a Buffer with the values at the same coordinates in
     * another Buffer using the minimum number of calls to
     * memcpy. Restricts itself to coordinates contained within the
     * intersection of the two buffers. If the two Buffers are not in
     * the same coordinate system, you will need to translate the
     * argument Buffer first. E.g. if you're blitting a sprite onto a
     * framebuffer, you'll want to translate the sprite to the correct
     * location first like so: \code
     * framebuffer.copy_from(sprite.translated({x, y})); \endcode
    */
    template<typename T2, int D2>
    void copy_from(const Buffer<T2, D2> &other) {
        Buffer<const T, D> src(other);
        Buffer<T, D> dst(*this);

        assert(src.dimensions() == dst.dimensions());

        // Trim the copy to the region in common
        for (int i = 0; i < dimensions(); i++) {
            int min_coord = std::max(dst.dim(i).min(), src.dim(i).min());
            int max_coord = std::min(dst.dim(i).max(), src.dim(i).max());
            if (max_coord < min_coord) {
                // The buffers do not overlap.
                return;
            }
            dst.crop(i, min_coord, max_coord - min_coord + 1);
            src.crop(i, min_coord, max_coord - min_coord + 1);
        }

        // Reorder the dimensions of dst to have strides in increasing
        // order. Apply the same transposition to src.
        for (int i = dimensions()-1; i > 0; i--) {
            for (int j = i; j > 0; j--) {
                if (dst.dim(j-1).stride() > dst.dim(j).stride()) {
                    dst.transpose(j-1, j);
                    src.transpose(j-1, j);
                }
            }
        }

        // Concatenate dense inner dimensions into contiguous memcpy tasks
        Buffer<const T, D> src_slice = src;
        Buffer<T, D> dst_slice = dst;
        int64_t slice_size = 1;
        while (dst_slice.dimensions() &&
               dst_slice.dim(0).stride() == slice_size &&
               src_slice.dim(0).stride() == slice_size) {
            slice_size *= dst_slice.dim(0).extent();
            int min_0 = dst_slice.dim(0).min();
            dst_slice = dst_slice.sliced(0, min_0);
            src_slice = src_slice.sliced(0, min_0);
        }
        slice_size *= buf.elem_size;

        // Do the memcpys
        src_slice.for_each_element([&](const int *pos) {
            memcpy(dst_slice.address_of(pos), src_slice.address_of(pos), slice_size);
        });
    }

    /** Make an image that refers to a sub-range of this image along
     * the given dimension. Does not assert the crop region is within
     * the existing bounds. The cropped image drops any device
     * handle. */
    Buffer<T, D> cropped(int d, int min, int extent) const {
        // Make a fresh copy of the underlying buffer (but not a fresh
        // copy of the allocation, if there is one).
        Buffer<T, D> im = *this;
        im.crop(d, min, extent);
        return im;
    }

    /** Crop an image in-place along the given dimension. */
    void crop(int d, int min, int extent) {
        // assert(dim(d).min() <= min);
        // assert(dim(d).max() >= min + extent - 1);
        int shift = min - dim(d).min();
        if (shift) {
            device_deallocate();
        }
        buf.host += shift * dim(d).stride() * buf.elem_size;
        buf.min[d] = min;
        buf.extent[d] = extent;
    }

    /** Make an image that refers to a sub-rectangle of this image along
     * the first N dimensions. Does not assert the crop region is within
     * the existing bounds. The cropped image drops any device handle. */
    Buffer<T, D> cropped(const std::vector<std::pair<int, int>> &rect) const {
        // Make a fresh copy of the underlying buffer (but not a fresh
        // copy of the allocation, if there is one).
        Buffer<T, D> im = *this;
        // Drop the reference to any device allocation. It won't be
        // valid for the cropped image.
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
    Buffer<T, D> translated(int d, int dx) const {
        Buffer<T, D> im = *this;
        im.translate(d, dx);
        return im;
    }

    /** Translate an image in-place along one dimension */
    void translate(int d, int delta) {
        device_deallocate();
        buf.min[d] += delta;
    }

    /** Make an image which refers to the same data translated along
     * the first N dimensions. */
    void translated(const std::vector<int> &delta) {
        Buffer<T, D> im = *this;
        im.translate(delta);
        return im;
    }

    /** Translate an image along the first N dimensions */
    void translate(const std::vector<int> &delta) {
        device_deallocate();
        for (size_t i = 0; i < delta.size(); i++) {
            translate(i, delta[i]);
        }
    }

    /** Set the min coordinate of an image in the first N dimensions */
    template<typename ...Args>
    void set_min(Args... args) {
        static_assert(sizeof...(args) <= D, "Too many arguments for dimensionality of Buffer");
        assert(sizeof...(args) <= (size_t)dimensions());
        device_deallocate();
        const int x[] = {args...};
        for (size_t i = 0; i < sizeof...(args); i++) {
            buf.min[i] = x[i];
        }
    }

    /** Make an image which refers to the same data using a different
     * ordering of the dimensions. */
    Buffer<T, D> transposed(int d1, int d2) const {
        Buffer<T, D> im = *this;
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
     * image. */
    Buffer<T, D-1> sliced(int d, int pos) const {
        Buffer<T, D> im = *this;
        im.slice(d, pos);
        return Buffer<T, D-1>(std::move(im));
    }

    /** Slice an image in-place */
    void slice(int d, int pos) {
        // assert(pos >= dim(d).min() && pos <= dim(d).max());
        device_deallocate();
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
     * the given min. This operation is the opposite of slice. As an
     * example, the following condition is true:
     *
     \code
     im2 = im.embedded(1, 17);
     &im(x, y, c) == &im2(x, 17, y, c);
     \endcode
     */
    Buffer<T, D+1> embedded(int d, int pos) const {
        assert(d >= 0 && d <= dimensions());
        Buffer<T, D+1> im(*this);
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
     * one. The stride is the extent of the outermost dimension times
     * its stride. The new dimension is the last dimension. This is a
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

    /** Add a new dimension with a min of zero, an extent of one, and
     * the specified stride. The new dimension is the last
     * dimension. This is a special case of embed. It requires that
     * the actual number of dimensions is less than template parameter
     * D. */
    void add_dimension_with_stride(int s) {
        add_dimension();
        buf.stride[dims-1] = s;
    }

    /** Call a callable at each location within the image. See
     * for_each_element below for more details. */
    template<typename Fn>
    void for_each_element(Fn f) const {
        Halide::for_each_element(buf, f);
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

    int copy_to_host(void *ctx = nullptr) {
        if (device_dirty()) {
            return halide_copy_to_host(ctx, &buf);
        }
        return 0;
    }

    int copy_to_device(const struct halide_device_interface *device_interface, void *ctx = nullptr) {
        if (host_dirty()) {
            return halide_copy_to_device(ctx, &buf, device_interface);
        }
        return 0;
    }

    int device_free(void *ctx = nullptr) {
        if (dev_ref_count) {
            // Multiple people may be holding onto this dev field
            assert(*dev_ref_count == 1 &&
                   "Multiple Halide::Buffer objects share this device "
                   "allocation. Freeing it would create dangling references. "
                   "Don't call device_free on Halide buffers that you have copied or "
                   "passed by value.");
        }        
        int ret = halide_device_free(ctx, &buf);
        if (dev_ref_count) {
            delete dev_ref_count;
            dev_ref_count = nullptr;
        }
        return ret;
    }

    int device_sync(void *ctx = nullptr) {
        return halide_device_sync(ctx, &buf);
    }

    bool has_device_allocation() const {
        return buf.dev;
    }
    // @}

    /** If you use the (x, y, c) indexing convention, then Halide
     * Buffers are stored planar by default. This function constructs
     * an interleaved RGB or RGBA image that can still be indexed
     * using (x, y, c). Passing it to a generator requires that the
     * generator has been compiled with support for interleaved (also
     * known as packed or chunky) memory layouts. */
    static Buffer<void, D> make_interleaved(halide_type_t t, int width, int height, int channels) {
        static_assert(D >= 3, "Not enough dimensions to make an interleaved image");
        Buffer<void, D> im(t, channels, width, height);
        im.transpose(0, 1);
        im.transpose(1, 2);
        return im;
    }

    /** If you use the (x, y, c) indexing convention, then Halide
     * Buffers are stored planar by default. This function constructs
     * an interleaved RGB or RGBA image that can still be indexed
     * using (x, y, c). Passing it to a generator requires that the
     * generator has been compiled with support for interleaved (also
     * known as packed or chunky) memory layouts. */
    static Buffer<T, D> make_interleaved(int width, int height, int channels) {
        static_assert(D >= 3, "Not enough dimensions to make an interleaved image");
        Buffer<T, D> im(channels, width, height);
        im.transpose(0, 1);
        im.transpose(1, 2);
        return im;
    }

    /** Wrap an existing interleaved image. */
    static Buffer<add_const_if_T_is_const<void>, D>
    make_interleaved(halide_type_t t, T *data, int width, int height, int channels) {
        static_assert(D >= 3, "Not enough dimensions to make an interleaved image");
        Buffer<add_const_if_T_is_const<void>, D> im(t, data, channels, width, height);
        im.transpose(0, 1);
        im.transpose(1, 2);
        return im;
    }

    /** Wrap an existing interleaved image. */
    static Buffer<T, D> make_interleaved(T *data, int width, int height, int channels) {
        static_assert(D >= 3, "Not enough dimensions to make an interleaved image");
        Buffer<T, D> im(data, channels, width, height);
        im.transpose(0, 1);
        im.transpose(1, 2);
        return im;
    }

    /** Make a zero-dimensional Buffer */
    static Buffer<add_const_if_T_is_const<void>, D> make_scalar(halide_type_t t) {
        Buffer<add_const_if_T_is_const<void>, 1> buf(t, 1);
        buf.slice(0, 0);
        return buf;
    }

    /** Make a zero-dimensional Buffer */
    static Buffer<T, D> make_scalar() {
        Buffer<T, 1> buf(1);
        buf.slice(0, 0);
        return buf;
    }

private:

    template<typename ...Args>
    ALWAYS_INLINE
    ptrdiff_t offset_of(int d, int first, Args... rest) const {
        return offset_of(d+1, rest...) + this->buf.stride[d] * (first - this->buf.min[d]);
    }

    ALWAYS_INLINE
    ptrdiff_t offset_of(int d) const {
        return 0;
    }

    template<typename ...Args>
    ALWAYS_INLINE
    storage_T *address_of(Args... args) const {
        if (T_is_void) {
            return (storage_T *)(this->buf.host) + offset_of(0, args...) * this->buf.elem_size;
        } else {
            return (storage_T *)(this->buf.host) + offset_of(0, args...);
        }
    }

    ALWAYS_INLINE
    ptrdiff_t offset_of(const int *pos) const {
        ptrdiff_t offset = 0;
        for (int i = this->dimensions() - 1; i >= 0; i--) {
            offset += this->buf.stride[i] * (pos[i] - this->buf.min[i]);
        }
        return offset;
    }

    ALWAYS_INLINE
    storage_T *address_of(const int *pos) const {
        if (T_is_void) {
            return (storage_T *)this->buf.host + offset_of(pos) * this->buf.elem_size;
        } else {
            return (storage_T *)this->buf.host + offset_of(pos);
        }
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
     * treated as their min coordinate. The non-const versions set the
     * host_dirty flag to true.
     */
    //@{
    template<typename ...Args,
             typename = typename std::enable_if<AllInts<Args...>::value>::type>
    ALWAYS_INLINE
    const not_void_T &operator()(int first, Args... rest) const {
        static_assert(!T_is_void,
                      "Cannot use operator() on Buffer<void> types");
        return *((const not_void_T *)(address_of(first, rest...)));
    }

    ALWAYS_INLINE
    const not_void_T &
    operator()() const {
        static_assert(!T_is_void,
                      "Cannot use operator() on Buffer<void> types");
        return *((const not_void_T *)(data()));
    }

    ALWAYS_INLINE
    const not_void_T &
    operator()(const int *pos) const {
        static_assert(!T_is_void,
                      "Cannot use operator() on Buffer<void> types");
        return *((const not_void_T *)(address_of(pos)));
    }

    template<typename ...Args,
             typename = typename std::enable_if<AllInts<Args...>::value>::type>
    ALWAYS_INLINE
    not_void_T &operator()(int first, Args... rest) {
        static_assert(!T_is_void,
                      "Cannot use operator() on Buffer<void> types");
        set_host_dirty();
        return *((not_void_T *)(address_of(first, rest...)));
    }

    ALWAYS_INLINE
    not_void_T &
    operator()() {
        static_assert(!T_is_void,
                      "Cannot use operator() on Buffer<void> types");
        set_host_dirty();
        return *((not_void_T *)(data()));
    }

    ALWAYS_INLINE
    not_void_T &
    operator()(const int *pos) {
        static_assert(!T_is_void,
                      "Cannot use operator() on Buffer<void> types");
        set_host_dirty();
        return *((not_void_T *)(address_of(pos)));
    }
    // @}

    /** Other calls to operator()(Args...) get redirected to a call to
     * image_accessor(const Buffer<T, D> &, Args...). This makes it
     * possible for later code to add new Buffer access methods for
     * types not convertible to int (e.g. Exprs). To add a custom
     * accessor, define an overload of image_accessor that takes the
     * expected arguments. See
     * test/correctness/custom_image_accessor.cpp for an example. */
    // @{
    template<typename ...Args>
    auto operator()(Args... args) const ->
        decltype(image_accessor(*this, args...)) {
        return image_accessor(*this, args...);
    }

    template<typename ...Args>
    auto operator()(Args... args) ->
        decltype(image_accessor(*this, args...)) {
        return image_accessor(*this, args...);
    }
    // @}

private:
    /** Helper functions for fill that call for_each_element with a
     * lambda of the correct dimensionality. */
    // @{
    template<typename ...Args>
    typename std::enable_if<(sizeof...(Args) < D)>::type
    fill_helper(not_void_T val, Args... args) {
       if (sizeof...(Args) == dimensions()) {
            for_each_element([&](Args... args) {(*this)(args...) = val;});
        } else {
            fill_helper(val, 0, args...);
        }
    }

    template<typename ...Args>
    typename std::enable_if<(sizeof...(Args) == D)>::type
    fill_helper(not_void_T val, Args...) {
        for_each_element([&](Args... args) {(*this)(args...) = val;});
    }
    // @}

public:

    /** Set every value in the buffer to the given value */
    void fill(not_void_T val) {
        static_assert(!T_is_void, "Can't fill a Buffer of unknown type");
        fill_helper(val);
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
    ALWAYS_INLINE
    static auto for_each_element_variadic(int, int d, Fn &&f, const buffer_t &buf, Args... args)
        -> decltype(f(args...)) {
        f(args...);
    }

    /** If the above overload is impossible, we add an outer loop over
     * an additional argument and try again. This trick is known as
     * SFINAE. */
    template<typename ...Args>
    ALWAYS_INLINE
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
    ALWAYS_INLINE
    static auto num_args(int, int *result, Fn &&f, Args... args) -> decltype(f(args...)) {
        *result = sizeof...(args);
        sink(std::forward<Fn>(f), args...);
    }

    /** The recursive version is only enabled up to a recursion limit
     * of 256. This catches callables that aren't callable with any
     * number of ints. */
    template<typename ...Args>
    ALWAYS_INLINE
    static void num_args(double, int *result, Fn &&f, Args... args) {
        static_assert(sizeof...(args) <= 256,
                      "Callable passed to for_each_element must accept either a const int *,"
                      " or up to 256 ints. No such operator found. Expect infinite template recursion.");
        return num_args(0, result, std::forward<Fn>(f), 0, args...);
    }

    ALWAYS_INLINE
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
    ALWAYS_INLINE
    static typename std::enable_if<d >= 0, void>::type
    for_each_element_array_helper(int, Fn &&f, const buffer_t &buf, int *pos) {
        for (pos[d] = buf.min[d]; pos[d] < buf.min[d] + buf.extent[d]; pos[d]++) {
            for_each_element_array_helper<d - 1>(0, std::forward<Fn>(f), buf, pos);
        }
    }

    /** Base case for recursion above. */
    template<int d>
    ALWAYS_INLINE
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
    ALWAYS_INLINE
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
Buffer<float, 3> im(100, 100, 3);
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
Buffer<float, 3> im(100, 100, 3);
for_each_element(im.sliced(1, 0), [&](int x, int c) {
    im(x, x, c) = 1.0f;
});
\endcode

* Or, assuming the memory layout is known to be dense per row, one can
* memset each row of an image like so:

Buffer<float, 3> im(100, 100, 3);
for_each_element(im.sliced(0, 0), [&](int y, int c) {
    memset(&im(0, y, c), 0, sizeof(float) * im.width());
});


\endcode

*/
template<typename Fn>
void for_each_element(const buffer_t &buf, Fn &&f) {
    for_each_element_helpers<Fn>::for_each_element(0, buf, std::forward<Fn>(f));
}

// Image is an alias for Buffer. Will be deprecated.
template<typename T = void, int D = 4> using Image = Buffer<T, D>;


}  // namespace Halide

#undef ALWAYS_INLINE

#endif  // HALIDE_RUNTIME_IMAGE_H

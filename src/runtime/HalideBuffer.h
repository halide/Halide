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
#include <algorithm>
#include <limits>
#include <stdint.h>
#include <string.h>

#include "HalideRuntime.h"

#ifdef _MSC_VER
#define HALIDE_ALLOCA _alloca
#else
#define HALIDE_ALLOCA __builtin_alloca
#endif

// gcc 5.1 has a false positive warning on this code
#if __GNUC__ == 5 && __GNUC_MINOR__ == 1
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

namespace Halide {
namespace Runtime {

// Forward-declare our Buffer class
template<typename T, int D> class Buffer;

// A helper to check if a parameter pack is entirely implicitly
// int-convertible to use with std::enable_if
template<typename ...Args>
struct AllInts : std::false_type {};

template<>
struct AllInts<> : std::true_type {};

template<typename T, typename ...Args>
struct AllInts<T, Args...> {
    static const bool value = std::is_convertible<T, int>::value && AllInts<Args...>::value;
};

// Floats and doubles are technically implicitly int-convertible, but
// doing so produces a warning we treat as an error, so just disallow
// it here.
template<typename ...Args>
struct AllInts<float, Args...> : std::false_type {};

template<typename ...Args>
struct AllInts<double, Args...> : std::false_type {};

/** A struct acting as a header for allocations owned by the Buffer
 * class itself. */
struct AllocationHeader {
    void (*deallocate_fn)(void *);
    std::atomic<int> ref_count;

    // Note that ref_count always starts at 1
    AllocationHeader(void (*deallocate_fn)(void *)) : deallocate_fn(deallocate_fn), ref_count(1) {}
};

/** This indicates how to deallocate the device for a Halide::Runtime::Buffer. */
enum struct BufferDeviceOwnership : int {
    Allocated,     ///> halide_device_free will be called when device ref count goes to zero
    WrappedNative, ///> halide_device_detach_native will be called when device ref count goes to zero
    Unmanaged,     ///> No free routine will be called when device ref count goes to zero
    AllocatedDeviceAndHost, ///> Call device_and_host_free when DevRefCount goes to zero.
    Cropped,       ///> Call halide_device_release_crop when DevRefCount goes to zero.
};

/** A similar struct for managing device allocations. */
struct DeviceRefCount {
    // This is only ever constructed when there's something to manage,
    // so start at one.
    std::atomic<int> count {1};
    BufferDeviceOwnership ownership{BufferDeviceOwnership::Allocated};
};

/** A templated Buffer class that wraps halide_buffer_t and adds
 * functionality. When using Halide from C++, this is the preferred
 * way to create input and output buffers. The overhead of using this
 * class relative to a naked halide_buffer_t is minimal - it uses another
 * ~16 bytes on the stack, and does no dynamic allocations when using
 * it to represent existing memory of a known maximum dimensionality.
 *
 * The template parameter T is the element type. For buffers where the
 * element type is unknown, or may vary, use void or const void.
 *
 * D is the maximum number of dimensions that can be represented using
 * space inside the class itself. Set it to the maximum dimensionality
 * you expect this buffer to be. If the actual dimensionality exceeds
 * this, heap storage is allocated to track the shape of the buffer. D
 * defaults to 4, which should cover nearly all usage.
 *
 * The class optionally allocates and owns memory for the image using
 * a shared pointer allocated with the provided allocator. If they are
 * null, malloc and free are used.  Any device-side allocation is
 * considered as owned if and only if the host-side allocation is
 * owned. */
template<typename T = void, int D = 4>
class Buffer {
    /** The underlying buffer_t */
    halide_buffer_t buf = {0};

    /** Some in-class storage for shape of the dimensions. */
    halide_dimension_t shape[D];

    /** The allocation owned by this Buffer. NULL if the Buffer does not
     * own the memory. */
    AllocationHeader *alloc = nullptr;

    /** A reference count for the device allocation owned by this
     * buffer. */
    mutable DeviceRefCount *dev_ref_count = nullptr;

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
    /** True if the Halide type is not void (or const void). */
    static constexpr bool has_static_halide_type = !T_is_void;

    /** Get the Halide type of T. Callers should not use the result if
     * has_static_halide_type is false. */
    static halide_type_t static_halide_type() {
        return halide_type_of<typename std::remove_cv<not_void_T>::type>();
    }

    /** Does this Buffer own the host memory it refers to? */
    bool owns_host_memory() const {
        return alloc != nullptr;
    }

private:
    /** Increment the reference count of any owned allocation */
    void incref() const {
        if (owns_host_memory()) {
            alloc->ref_count++;
        }
        if (buf.device) {
            if (!dev_ref_count) {
                // I seem to have a non-zero dev field but no
                // reference count for it. I must have been given a
                // device allocation by a Halide pipeline, and have
                // never been copied from since. Take sole ownership
                // of it.
                dev_ref_count = new DeviceRefCount;
            }
            dev_ref_count->count++;
        }
    }

    struct DevRefCountCropped : DeviceRefCount {
        Buffer<T, D> cropped_from;
        DevRefCountCropped(const Buffer<T, D> &cropped_from) : cropped_from(cropped_from) {
            ownership = BufferDeviceOwnership::Cropped;
        }
    };

    /** Setup the device ref count for a buffer to indicate it is a crop of cropped_from */
    void crop_from(const Buffer<T, D> &cropped_from) {
        assert(dev_ref_count == nullptr);
        dev_ref_count = new DevRefCountCropped(cropped_from);
    }

    /** Decrement the reference count of any owned allocation and free host
     * and device memory if it hits zero. Sets alloc to nullptr. */
    void decref() {
        if (owns_host_memory()) {
            int new_count = --(alloc->ref_count);
            if (new_count == 0) {
                void (*fn)(void *) = alloc->deallocate_fn;
                alloc->~AllocationHeader();
                fn(alloc);
            }
            buf.host = nullptr;
            alloc = nullptr;
            set_host_dirty(false);
        }
        decref_dev();
    }

    void decref_dev() {
        int new_count = 0;
        if (dev_ref_count) {
            new_count = --(dev_ref_count->count);
        }
        if (new_count == 0) {
            if (buf.device) {
                assert(!(alloc && device_dirty()) &&
                       "Implicitly freeing a dirty device allocation while a host allocation still lives. "
                       "Call device_free explicitly if you want to drop dirty device-side data. "
                       "Call copy_to_host explicitly if you want the data copied to the host allocation "
                       "before the device allocation is freed.");
                if (dev_ref_count && dev_ref_count->ownership == BufferDeviceOwnership::WrappedNative) {
                    buf.device_interface->detach_native(nullptr, &buf);
                } else if (dev_ref_count && dev_ref_count->ownership == BufferDeviceOwnership::AllocatedDeviceAndHost) {
                    buf.device_interface->device_and_host_free(nullptr, &buf);
                } else if (dev_ref_count && dev_ref_count->ownership == BufferDeviceOwnership::Cropped) {
                    buf.device_interface->device_release_crop(nullptr, &buf);
                } else if (dev_ref_count == nullptr || dev_ref_count->ownership == BufferDeviceOwnership::Allocated) {
                    buf.device_interface->device_free(nullptr, &buf);
                }
            }
            if (dev_ref_count) {
                if (dev_ref_count->ownership == BufferDeviceOwnership::Cropped) {
                    delete (DevRefCountCropped *)dev_ref_count;
                } else {
                    delete dev_ref_count;
                }
            }
        }
        buf.device = 0;
        buf.device_interface = nullptr;
        dev_ref_count = nullptr;
    }

    void free_shape_storage() {
        if (buf.dim != shape) {
            delete[] buf.dim;
            buf.dim = nullptr;
        }
    }

    void make_shape_storage() {
        if (buf.dimensions <= D) {
            buf.dim = shape;
        } else {
            buf.dim = new halide_dimension_t[buf.dimensions];
        }
    }

    void copy_shape_from(const halide_buffer_t &other) {
        // All callers of this ensure that buf.dimensions == other.dimensions.
        make_shape_storage();
        for (int i = 0; i < buf.dimensions; i++) {
            buf.dim[i] = other.dim[i];
        }
    }

    template<typename T2, int D2>
    void move_shape_from(Buffer<T2, D2> &&other) {
        if (other.shape == other.buf.dim) {
            copy_shape_from(other.buf);
        } else {
            buf.dim = other.buf.dim;
            other.buf.dim = nullptr;
        }
    }

    /** Initialize the shape from a halide_buffer_t. */
    void initialize_from_buffer(const halide_buffer_t &b,
                                BufferDeviceOwnership ownership) {
        memcpy(&buf, &b, sizeof(halide_buffer_t));
        copy_shape_from(b);
        if (b.device) {
            dev_ref_count = new DeviceRefCount;
            dev_ref_count->ownership = ownership;
        }
    }

    /** Initialize the shape from a parameter pack of ints */
    template<typename ...Args>
    void initialize_shape(int next, int first, Args... rest) {
        buf.dim[next].min = 0;
        buf.dim[next].extent = first;
        if (next == 0) {
            buf.dim[next].stride = 1;
        } else {
            buf.dim[next].stride = buf.dim[next-1].stride * buf.dim[next-1].extent;
        }
        initialize_shape(next + 1, rest...);
    }

    /** Base case for the template recursion above. */
    void initialize_shape(int) {
    }

    /** Initialize the shape from a vector of extents */
    void initialize_shape(const std::vector<int> &sizes) {
        assert(sizes.size() <= std::numeric_limits<int>::max());
        int limit = (int)sizes.size();
        assert(limit <= dimensions());
        for (int i = 0; i < limit; i++) {
            buf.dim[i].min = 0;
            buf.dim[i].extent = sizes[i];
            if (i == 0) {
                buf.dim[i].stride = 1;
            } else {
                buf.dim[i].stride = buf.dim[i-1].stride * buf.dim[i-1].extent;
            }
        }
    }

    /** Initialize the shape from the static shape of an array */
    template<typename Array, size_t N>
    void initialize_shape_from_array_shape(int next, Array (&vals)[N]) {
        buf.dim[next].min = 0;
        buf.dim[next].extent = (int)N;
        if (next == 0) {
            buf.dim[next].stride = 1;
        } else {
            initialize_shape_from_array_shape(next - 1, vals[0]);
            buf.dim[next].stride = buf.dim[next - 1].stride * buf.dim[next - 1].extent;
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

    /** Crop a single dimension without handling device allocation. */
    void crop_host(int d, int min, int extent) {
        // TODO(abadams|zvookin): these asserts fail on correctness_autotune_bug
        // due to unsafe crop in Func::infer_input_bounds. See comment at Func.cpp:2834.
        // Should either fix that or kill the asserts and document the routine accordingly.
        //        assert(dim(d).min() <= min);
        //        assert(dim(d).max() >= min + extent - 1);
        int shift = min - dim(d).min();
        if (buf.host != nullptr) {
            buf.host += shift * dim(d).stride() * type().bytes();
        }
        buf.dim[d].min = min;
        buf.dim[d].extent = extent;
    }

    /** Crop as many dimensions as are in rect, without handling device allocation. */
    void crop_host(const std::vector<std::pair<int, int>> &rect) {
        assert(rect.size() <= std::numeric_limits<int>::max());
        int limit = (int)rect.size();
        assert(limit <= dimensions());
        for (int i = 0; i < limit; i++) {
            crop_host(i, rect[i].first, rect[i].second);
        }
    }

    void complete_device_crop(Buffer<T, D> &result_host_cropped) const {
        assert(buf.device_interface != nullptr);
        if (buf.device_interface->device_crop(nullptr, &this->buf, &result_host_cropped.buf) == 0) {
            const Buffer<T, D> *cropped_from = this;
            // TODO: Figure out what to do if dev_ref_count is nullptr. Should incref logic run here?
            // is it possible to get to this point without incref having run at least once since
            // the device field was set? (I.e. in the internal logic of crop. incref might have been
            // called.)
            if (dev_ref_count != nullptr && dev_ref_count->ownership == BufferDeviceOwnership::Cropped) {
                cropped_from = &((DevRefCountCropped *)dev_ref_count)->cropped_from;
            }
            result_host_cropped.crop_from(*cropped_from);
        }
    }

public:

    typedef T ElemType;

    /** Read-only access to the shape */
    class Dimension {
        const halide_dimension_t &d;
    public:
        /** The lowest coordinate in this dimension */
        HALIDE_ALWAYS_INLINE int min() const {
            return d.min;
        }

        /** The number of elements in memory you have to step over to
         * increment this coordinate by one. */
        HALIDE_ALWAYS_INLINE int stride() const {
            return d.stride;
        }

        /** The extent of the image along this dimension */
        HALIDE_ALWAYS_INLINE int extent() const {
            return d.extent;
        }

        /** The highest coordinate in this dimension */
        HALIDE_ALWAYS_INLINE int max() const {
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
        HALIDE_ALWAYS_INLINE iterator begin() const {
            return {min()};
        }

        /** An iterator that points to one past the max coordinate */
        HALIDE_ALWAYS_INLINE iterator end() const {
            return {min() + extent()};
        }

        Dimension(const halide_dimension_t &dim) : d(dim) {};
    };

    /** Access the shape of the buffer */
    HALIDE_ALWAYS_INLINE Dimension dim(int i) const {
        return Dimension(buf.dim[i]);
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
        return buf.dimensions;
    }

    /** Get the type of the elements. */
    halide_type_t type() const {
        return buf.type;
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
        return (T *)(buf.host + index * type().bytes());
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
        return (T *)(buf.host + index * type().bytes());
    }

    /** The total number of bytes spanned by the data in memory. */
    size_t size_in_bytes() const {
        return (size_t)((const uint8_t *)end() - (const uint8_t *)begin());
    }

    Buffer() : shape() {
        buf.type = static_halide_type();
        make_shape_storage();
    }

    /** Make a Buffer from a halide_buffer_t */
    Buffer(const halide_buffer_t &buf,
           BufferDeviceOwnership ownership = BufferDeviceOwnership::Unmanaged) {
        assert(T_is_void || buf.type == static_halide_type());
        initialize_from_buffer(buf, ownership);
    }

    /** Make a Buffer from a legacy buffer_t. */
    Buffer(const buffer_t &old_buf) {
        assert(!T_is_void && old_buf.elem_size == static_halide_type().bytes());
        buf.host = old_buf.host;
        buf.type = static_halide_type();
        int d;
        for (d = 0; d < 4 && old_buf.extent[d]; d++);
        buf.dimensions = d;
        make_shape_storage();
        for (int i = 0; i < d; i++) {
            buf.dim[i].min = old_buf.min[i];
            buf.dim[i].extent = old_buf.extent[i];
            buf.dim[i].stride = old_buf.stride[i];
        }
        buf.set_host_dirty(old_buf.host_dirty);
        assert(old_buf.dev == 0 && "Cannot construct a Halide::Runtime::Buffer from a legacy buffer_t with a device allocation. Use halide_upgrade_buffer_t to upgrade it to a halide_buffer_t first.");
    }

    /** Populate the fields of a legacy buffer_t using this
     * Buffer. Does not copy device metadata. */
    buffer_t make_legacy_buffer_t() const {
        buffer_t old_buf = {0};
        assert(!has_device_allocation() && "Cannot construct a legacy buffer_t from a Halide::Runtime::Buffer with a device allocation. Use halide_downgrade_buffer_t instead.");
        old_buf.host = buf.host;
        old_buf.elem_size = buf.type.bytes();
        assert(dimensions() <= 4 && "Cannot construct a legacy buffer_t from a Halide::Runtime::Buffer with more than four dimensions.");
        for (int i = 0; i < dimensions(); i++) {
            old_buf.min[i] = dim(i).min();
            old_buf.extent[i] = dim(i).extent();
            old_buf.stride[i] = dim(i).stride();
        }
        return old_buf;
    }

    /** Give Buffers access to the members of Buffers of different dimensionalities and types. */
    template<typename T2, int D2> friend class Buffer;

    /** Determine if if an Buffer<T, D> can be constructed from some other Buffer type.
     * If this can be determined at compile time, fail with a static assert; otherwise
     * return a boolean based on runtime typing. */
    template<typename T2, int D2>
    static bool can_convert_from(const Buffer<T2, D2> &other) {
        static_assert((!std::is_const<T2>::value || std::is_const<T>::value),
                      "Can't convert from a Buffer<const T> to a Buffer<T>");
        static_assert(std::is_same<typename std::remove_const<T>::type,
                                   typename std::remove_const<T2>::type>::value ||
                      T_is_void || Buffer<T2, D2>::T_is_void,
                      "type mismatch constructing Buffer");
        if (Buffer<T2, D2>::T_is_void && !T_is_void) {
            return other.type() == static_halide_type();
        }
        return true;
    }

    /** Fail an assertion at runtime or compile-time if an Buffer<T, D>
     * cannot be constructed from some other Buffer type. */
    template<typename T2, int D2>
    static void assert_can_convert_from(const Buffer<T2, D2> &other) {
        assert(can_convert_from(other));
    }

    /** Copy constructor. Does not copy underlying data. */
    Buffer(const Buffer<T, D> &other) : buf(other.buf),
                                        alloc(other.alloc) {
        other.incref();
        dev_ref_count = other.dev_ref_count;
        copy_shape_from(other.buf);
    }

    /** Construct a Buffer from a Buffer of different dimensionality
     * and type. Asserts that the type matches (at runtime, if one of
     * the types is void). Note that this constructor is
     * implicit. This, for example, lets you pass things like
     * Buffer<T> or Buffer<const void> to functions expected
     * Buffer<const T>. */
    template<typename T2, int D2>
    Buffer(const Buffer<T2, D2> &other) : buf(other.buf),
                                          alloc(other.alloc) {
        assert_can_convert_from(other);
        other.incref();
        dev_ref_count = other.dev_ref_count;
        copy_shape_from(other.buf);
    }

    /** Move constructor */
    Buffer(Buffer<T, D> &&other) : buf(other.buf),
                                   alloc(other.alloc),
                                   dev_ref_count(other.dev_ref_count) {
        other.dev_ref_count = nullptr;
        other.alloc = nullptr;
        other.buf.device = 0;
        other.buf.device_interface = nullptr;
        move_shape_from(std::forward<Buffer<T, D>>(other));
    }

    /** Move-construct a Buffer from a Buffer of different
     * dimensionality and type. Asserts that the types match (at
     * runtime if one of the types is void). */
    template<typename T2, int D2>
    Buffer(Buffer<T2, D2> &&other) : buf(other.buf),
                                     alloc(other.alloc),
                                     dev_ref_count(other.dev_ref_count) {
        other.dev_ref_count = nullptr;
        other.alloc = nullptr;
        other.buf.device = 0;
        other.buf.device_interface = nullptr;
        move_shape_from(std::forward<Buffer<T2, D2>>(other));
    }

    /** Assign from another Buffer of possibly-different
     * dimensionality and type. Asserts that the types match (at
     * runtime if one of the types is void). */
    template<typename T2, int D2>
    Buffer<T, D> &operator=(const Buffer<T2, D2> &other) {
        if ((const void *)this == (const void *)&other) {
            return *this;
        }
        assert_can_convert_from(other);
        other.incref();
        decref();
        dev_ref_count = other.dev_ref_count;
        alloc = other.alloc;
        free_shape_storage();
        buf = other.buf;
        copy_shape_from(other.buf);
        return *this;
    }

    /** Standard assignment operator */
    Buffer<T, D> &operator=(const Buffer<T, D> &other) {
        if (this == &other) {
            return *this;
        }
        other.incref();
        decref();
        dev_ref_count = other.dev_ref_count;
        alloc = other.alloc;
        free_shape_storage();
        buf = other.buf;
        copy_shape_from(other.buf);
        return *this;
    }

    /** Move from another Buffer of possibly-different
     * dimensionality and type. Asserts that the types match (at
     * runtime if one of the types is void). */
    template<typename T2, int D2>
    Buffer<T, D> &operator=(Buffer<T2, D2> &&other) {
        assert_can_convert_from(other);
        decref();
        alloc = other.alloc;
        other.alloc = nullptr;
        dev_ref_count = other.dev_ref_count;
        other.dev_ref_count = nullptr;
        free_shape_storage();
        buf = other.buf;
        other.buf.device = 0;
        other.buf.device_interface = nullptr;
        move_shape_from(std::forward<Buffer<T2, D2>>(other));
        return *this;
    }

    /** Standard move-assignment operator */
    Buffer<T, D> &operator=(Buffer<T, D> &&other) {
        decref();
        alloc = other.alloc;
        other.alloc = nullptr;
        dev_ref_count = other.dev_ref_count;
        other.dev_ref_count = nullptr;
        free_shape_storage();
        buf = other.buf;
        other.buf.device = 0;
        other.buf.device_interface = nullptr;
        move_shape_from(std::forward<Buffer<T, D>>(other));
        return *this;
    }

    /** Check the product of the extents fits in memory. */
    void check_overflow() {
        size_t size = type().bytes();
        for (int i = 0; i < dimensions(); i++) {
            size *= dim(i).extent();
        }
        // We allow 2^31 or 2^63 bytes, so drop the top bit.
        size = (size << 1) >> 1;
        for (int i = 0; i < dimensions(); i++) {
            size /= dim(i).extent();
        }
        assert(size == (size_t)type().bytes() && "Error: Overflow computing total size of buffer.");
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
        void *alloc_storage = allocate_fn(size + sizeof(AllocationHeader) + alignment - 1);
        alloc = new (alloc_storage) AllocationHeader(deallocate_fn);
        uint8_t *unaligned_ptr = ((uint8_t *)alloc) + sizeof(AllocationHeader);
        buf.host = (uint8_t *)((uintptr_t)(unaligned_ptr + alignment - 1) & ~(alignment - 1));
    }

    /** Drop reference to any owned host or device memory, possibly
     * freeing it, if this buffer held the last reference to
     * it. Retains the shape of the buffer. Does nothing if this
     * buffer did not allocate its own memory. */
    void deallocate() {
        decref();
    }

    /** Drop reference to any owned device memory, possibly freeing it
     * if this buffer held the last reference to it. Asserts that
     * device_dirty is false. */
    void device_deallocate() {
        decref_dev();
    }

    /** Allocate a new image of the given size with a runtime
     * type. Only used when you do know what size you want but you
     * don't know statically what type the elements are. Pass zeroes
     * to make a buffer suitable for bounds query calls. */
    template<typename ...Args,
             typename = typename std::enable_if<AllInts<Args...>::value>::type>
    Buffer(halide_type_t t, int first, Args... rest) {
        if (!T_is_void) {
            assert(static_halide_type() == t);
        }
        buf.type = t;
        buf.dimensions = 1 + (int)(sizeof...(rest));
        make_shape_storage();
        initialize_shape(0, first, rest...);
        if (!any_zero(first, rest...)) {
            check_overflow();
            allocate();
        }
    }


    /** Allocate a new image of the given size. Pass zeroes to make a
     * buffer suitable for bounds query calls. */
    // @{

    // The overload with one argument is 'explicit', so that
    // (say) int is not implicitly convertable to Buffer<int>
    explicit Buffer(int first) {
        static_assert(!T_is_void,
                      "To construct an Buffer<void>, pass a halide_type_t as the first argument to the constructor");
        buf.type = static_halide_type();
        buf.dimensions = 1;
        make_shape_storage();
        initialize_shape(0, first);
        if (first != 0) {
            check_overflow();
            allocate();
        }
    }

    template<typename ...Args,
             typename = typename std::enable_if<AllInts<Args...>::value>::type>
    Buffer(int first, int second, Args... rest) {
        static_assert(!T_is_void,
                      "To construct an Buffer<void>, pass a halide_type_t as the first argument to the constructor");
        buf.type = static_halide_type();
        buf.dimensions = 2 + (int)(sizeof...(rest));
        make_shape_storage();
        initialize_shape(0, first, second, rest...);
        if (!any_zero(first, second, rest...)) {
            check_overflow();
            allocate();
        }
    }
    // @}

    /** Allocate a new image of unknown type using a vector of ints as the size. */
    Buffer(halide_type_t t, const std::vector<int> &sizes) {
        if (!T_is_void) {
            assert(static_halide_type() == t);
        }
        buf.type = t;
        buf.dimensions = (int)sizes.size();
        make_shape_storage();
        initialize_shape(sizes);
        if (!any_zero(sizes)) {
            check_overflow();
            allocate();
        }
    }

    /** Allocate a new image of known type using a vector of ints as the size. */
    Buffer(const std::vector<int> &sizes) {
        buf.type = static_halide_type();
        buf.dimensions = (int)sizes.size();
        make_shape_storage();
        initialize_shape(sizes);
        if (!any_zero(sizes)) {
            check_overflow();
            allocate();
        }
    }

    /** Make an Buffer that refers to a statically sized array. Does not
     * take ownership of the data, and does not set the host_dirty flag. */
    template<typename Array, size_t N>
    explicit Buffer(Array (&vals)[N]) {
        buf.dimensions = dimensionality_of_array(vals);
        buf.type = scalar_type_of_array(vals);
        buf.host = (uint8_t *)vals;
        make_shape_storage();
        initialize_shape_from_array_shape(buf.dimensions - 1, vals);
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
        buf.type = t;
        buf.dimensions = 1 + (int)(sizeof...(rest));
        buf.host = (uint8_t *) const_cast<void *>(data);
        make_shape_storage();
        initialize_shape(0, first, int(rest)...);
    }

    /** Initialize an Buffer from a pointer and some sizes. Assumes
     * dense row-major packing and a min coordinate of zero. Does not
     * take ownership of the data and does not set the host_dirty flag. */
    template<typename ...Args,
             typename = typename std::enable_if<AllInts<Args...>::value>::type>
    explicit Buffer(T *data, int first, Args&&... rest) {
        buf.type = static_halide_type();
        buf.dimensions = 1 + (int)(sizeof...(rest));
        buf.host = (uint8_t *) const_cast<typename std::remove_const<T>::type *>(data);
        make_shape_storage();
        initialize_shape(0, first, int(rest)...);
    }

    /** Initialize an Buffer from a pointer and a vector of
     * sizes. Assumes dense row-major packing and a min coordinate of
     * zero. Does not take ownership of the data and does not set the
     * host_dirty flag. */
    explicit Buffer(T *data, const std::vector<int> &sizes) {
        buf.type = static_halide_type();
        buf.dimensions = (int)sizes.size();
        buf.host = (uint8_t *) const_cast<typename std::remove_const<T>::type *>(data);
        make_shape_storage();
        initialize_shape(sizes);
    }

    /** Initialize an Buffer of runtime type from a pointer and a
     * vector of sizes. Assumes dense row-major packing and a min
     * coordinate of zero. Does not take ownership of the data and
     * does not set the host_dirty flag. */
    explicit Buffer(halide_type_t t, add_const_if_T_is_const<void> *data, const std::vector<int> &sizes) {
        if (!T_is_void) {
            assert(static_halide_type() == t);
        }
        buf.type = t;
        buf.dimensions = (int)sizes.size();
        buf.host = (uint8_t *) const_cast<void *>(data);
        make_shape_storage();
        initialize_shape(sizes);
    }

    /** Initialize an Buffer from a pointer to the min coordinate and
     * an array describing the shape.  Does not take ownership of the
     * data, and does not set the host_dirty flag. */
    explicit Buffer(halide_type_t t, add_const_if_T_is_const<void> *data, int d, const halide_dimension_t *shape) {
        if (!T_is_void) {
            assert(static_halide_type() == t);
        }
        buf.type = t;
        buf.dimensions = d;
        buf.host = (uint8_t *) const_cast<void *>(data);
        make_shape_storage();
        for (int i = 0; i < d; i++) {
            buf.dim[i] = shape[i];
        }
    }

    /** Initialize an Buffer from a pointer to the min coordinate and
     * an array describing the shape.  Does not take ownership of the
     * data and does not set the host_dirty flag. */
    explicit Buffer(T *data, int d, const halide_dimension_t *shape) {
        buf.type = halide_type_of<typename std::remove_cv<T>::type>();
        buf.dimensions = d;
        buf.host = (uint8_t *) const_cast<typename std::remove_const<T>::type *>(data);
        make_shape_storage();
        for (int i = 0; i < d; i++) {
            buf.dim[i] = shape[i];
        }
    }

    /** Destructor. Will release any underlying owned allocation if
     * this is the last reference to it. Will assert fail if there are
     * weak references to this Buffer outstanding. */
    ~Buffer() {
        free_shape_storage();
        decref();
    }

    /** Get a pointer to the raw halide_buffer_t this wraps. */
    // @{
    halide_buffer_t *raw_buffer() {
        return &buf;
    }

    const halide_buffer_t *raw_buffer() const {
        return &buf;
    }
    // @}

    /** Provide a cast operator to halide_buffer_t *, so that
     * instances can be passed directly to Halide filters. */
    operator halide_buffer_t *() {
        return &buf;
    }

    /** Return a typed reference to this Buffer. Useful for converting
     * a reference to a Buffer<void> to a reference to, for example, a
     * Buffer<const uint8_t>. Does a runtime assert if the source
     * buffer type is void. */
    template<typename T2, int D2 = D,
             typename = typename std::enable_if<(D2 <= D)>::type>
    Buffer<T2, D2> &as() & {
        Buffer<T2, D>::assert_can_convert_from(*this);
        return *((Buffer<T2, D2> *)this);
    }

    /** Return a const typed reference to this Buffer. Useful for
     * converting a conference reference to one Buffer type to a const
     * reference to another Buffer type. Does a runtime assert if the
     * source buffer type is void. */
    template<typename T2, int D2 = D,
             typename = typename std::enable_if<(D2 <= D)>::type>
    const Buffer<T2, D2> &as() const &  {
        Buffer<T2, D>::assert_can_convert_from(*this);
        return *((const Buffer<T2, D2> *)this);
    }

    /** Returns this rval Buffer with a different type attached. Does
     * a dynamic type check if the source type is void. */
    template<typename T2, int D2 = D>
    Buffer<T2, D2> as() && {
        Buffer<T2, D2>::assert_can_convert_from(*this);
        return *((Buffer<T2, D2> *)this);
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

    /** Make a new image which is a deep copy of this image. Use crop
     * or slice followed by copy to make a copy of only a portion of
     * the image. The new image uses the same memory layout as the
     * original, with holes compacted away. */
    Buffer<T, D> copy(void *(*allocate_fn)(size_t) = nullptr,
                      void (*deallocate_fn)(void *) = nullptr) const {
        Buffer<T, D> dst = make_with_shape_of(*this, allocate_fn, deallocate_fn);
        dst.copy_from(*this);
        return dst;
    }

    /** Fill a Buffer with the values at the same coordinates in
     * another Buffer. Restricts itself to coordinates contained
     * within the intersection of the two buffers. If the two Buffers
     * are not in the same coordinate system, you will need to
     * translate the argument Buffer first. E.g. if you're blitting a
     * sprite onto a framebuffer, you'll want to translate the sprite
     * to the correct location first like so: \code
     * framebuffer.copy_from(sprite.translated({x, y})); \endcode
    */
    template<typename T2, int D2>
    void copy_from(const Buffer<T2, D2> &other) {
        assert(!device_dirty() && "Cannot call Halide::Runtime::Buffer::copy_from on a device dirty destination.");
        assert(!other.device_dirty() && "Cannot call Halide::Runtime::Buffer::copy_from on a device dirty source.");

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

        // If T is void, we need to do runtime dispatch to an
        // appropriately-typed lambda. We're copying, so we only care
        // about the element size.
        if (type().bytes() == 1) {
            using MemType = uint8_t;
            auto &typed_dst = (Buffer<MemType, D> &)dst;
            auto &typed_src = (Buffer<const MemType, D> &)src;
            typed_dst.for_each_value([&](MemType &dst, MemType src) {dst = src;}, typed_src);
        } else if (type().bytes() == 2) {
            using MemType = uint16_t;
            auto &typed_dst = (Buffer<MemType, D> &)dst;
            auto &typed_src = (Buffer<const MemType, D> &)src;
            typed_dst.for_each_value([&](MemType &dst, MemType src) {dst = src;}, typed_src);
        } else if (type().bytes() == 4) {
            using MemType = uint32_t;
            auto &typed_dst = (Buffer<MemType, D> &)dst;
            auto &typed_src = (Buffer<const MemType, D> &)src;
            typed_dst.for_each_value([&](MemType &dst, MemType src) {dst = src;}, typed_src);
        } else if (type().bytes() == 8) {
            using MemType = uint64_t;
            auto &typed_dst = (Buffer<MemType, D> &)dst;
            auto &typed_src = (Buffer<const MemType, D> &)src;
            typed_dst.for_each_value([&](MemType &dst, MemType src) {dst = src;}, typed_src);
        } else {
            assert(false && "type().bytes() must be 1, 2, 4, or 8");
        }
        set_host_dirty();
    }

    /** Make an image that refers to a sub-range of this image along
     * the given dimension. Does not assert the crop region is within
     * the existing bounds. */
    Buffer<T, D> cropped(int d, int min, int extent) const {
        // Make a fresh copy of the underlying buffer (but not a fresh
        // copy of the allocation, if there is one).
        Buffer<T, D> im = *this;

        // This guarantees the prexisting device ref is dropped if the
        // device_crop call fails and maintains the buffer in a consistent
        // state.
        im.device_deallocate();

        im.crop_host(d, min, extent);
        if (buf.device_interface != nullptr) {
            complete_device_crop(im);
        }
        return im;
    }

    /** Crop an image in-place along the given dimension. */
    void crop(int d, int min, int extent) {
        // An optimization for non-device buffers. For the device case,
        // a temp buffer is required, so reuse the not-in-place version.
        // TODO(zalman|abadams): Are nop crops common enough to special
        // case the device part of the if to do nothing?
        if (buf.device_interface != nullptr) {
            *this = cropped(d, min, extent);
        } else {
            crop_host(d, min, extent);
        }
    }

    /** Make an image that refers to a sub-rectangle of this image along
     * the first N dimensions. Does not assert the crop region is within
     * the existing bounds. The cropped image drops any device handle. */
    Buffer<T, D> cropped(const std::vector<std::pair<int, int>> &rect) const {
        // Make a fresh copy of the underlying buffer (but not a fresh
        // copy of the allocation, if there is one).
        Buffer<T, D> im = *this;

        // This guarantees the prexisting device ref is dropped if the
        // device_crop call fails and maintains the buffer in a consistent
        // state.
        im.device_deallocate();

        im.crop_host(rect);
        if (buf.device_interface != nullptr) {
            complete_device_crop(im);
        }
        return im;
    }

    /** Crop an image in-place along the first N dimensions. */
    void crop(const std::vector<std::pair<int, int>> &rect) {
        // An optimization for non-device buffers. For the device case,
        // a temp buffer is required, so reuse the not-in-place version.
        // TODO(zalman|abadams): Are nop crops common enough to special
        // case the device part of the if to do nothing?
        if (buf.device_interface != nullptr) {
            *this = cropped(rect);
        } else {
            crop_host(rect);
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
        buf.dim[d].min += delta;
    }

    /** Make an image which refers to the same data translated along
     * the first N dimensions. */
    Buffer<T, D> translated(const std::vector<int> &delta) {
        Buffer<T, D> im = *this;
        im.translate(delta);
        return im;
    }

    /** Translate an image along the first N dimensions */
    void translate(const std::vector<int> &delta) {
        device_deallocate();
        assert(delta.size() <= std::numeric_limits<int>::max());
        int limit = (int)delta.size();
        assert(limit <= dimensions());
        for (int i = 0; i < limit; i++) {
            translate(i, delta[i]);
        }
    }

    /** Set the min coordinate of an image in the first N dimensions */
    // @{
    void set_min(const std::vector<int> &mins) {
        assert(mins.size() <= (size_t)dimensions());
        device_deallocate();
        for (size_t i = 0; i < mins.size(); i++) {
            buf.dim[i].min = mins[i];
        }
    }

    template<typename ...Args>
    void set_min(Args... args) {
        set_min(std::vector<int>{args...});
    }
    // @}

    /** Test if a given coordinate is within the the bounds of an image */
    // @{
    bool contains(const std::vector<int> &coords) const {
        assert(coords.size() <= (size_t)dimensions());
        for (size_t i = 0; i < coords.size(); i++) {
            if (coords[i] < dim((int) i).min() || coords[i] > dim((int) i).max()) {
                return false;
            }
        }
        return true;
    }

    template<typename ...Args>
    bool contains(Args... args) const {
        return contains(std::vector<int>{args...});
    }
    // @}

    /** Make an image which refers to the same data using a different
     * ordering of the dimensions. */
    Buffer<T, D> transposed(int d1, int d2) const {
        Buffer<T, D> im = *this;
        im.transpose(d1, d2);
        return im;
    }

    /** Transpose an image in-place */
    void transpose(int d1, int d2) {
        std::swap(buf.dim[d1], buf.dim[d2]);
    }

    /** Make a lower-dimensional image that refers to one slice of this
     * image. */
    Buffer<T, D> sliced(int d, int pos) const {
        Buffer<T, D> im = *this;
        im.slice(d, pos);
        return im;
    }

    /** Slice an image in-place */
    void slice(int d, int pos) {
        // assert(pos >= dim(d).min() && pos <= dim(d).max());
        device_deallocate();
        buf.dimensions--;
        int shift = pos - dim(d).min();
        assert(buf.device == 0 || shift == 0);
        if (buf.host != nullptr) {
            buf.host += shift * dim(d).stride() * type().bytes();
        }
        for (int i = d; i < dimensions(); i++) {
            buf.dim[i] = buf.dim[i+1];
        }
        buf.dim[buf.dimensions] = {0, 0, 0};
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
    Buffer<T, D> embedded(int d, int pos) const {
        assert(d >= 0 && d <= dimensions());
        Buffer<T, D> im(*this);
        im.embed(d, pos);
        return im;
    }

    /** Embed an image in-place, increasing the
     * dimensionality. */
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
     * special case of embed. */
    void add_dimension() {
        const int dims = buf.dimensions;
        buf.dimensions++;
        if (buf.dim != shape) {
            // We're already on the heap. Reallocate.
            halide_dimension_t *new_shape = new halide_dimension_t[buf.dimensions];
            for (int i = 0; i < dims; i++) {
                new_shape[i] = buf.dim[i];
            }
            delete[] buf.dim;
            buf.dim = new_shape;
        } else if (dims == D) {
            // Transition from the in-class storage to the heap
            make_shape_storage();
            for (int i = 0; i < dims; i++) {
                buf.dim[i] = shape[i];
            }
        } else {
            // We still fit in the class
        }
        buf.dim[dims] = {0, 1, 0};
        if (dims == 0) {
            buf.dim[dims].stride = 1;
        } else {
            buf.dim[dims].stride = buf.dim[dims-1].extent * buf.dim[dims-1].stride;
        }
    }

    /** Add a new dimension with a min of zero, an extent of one, and
     * the specified stride. The new dimension is the last
     * dimension. This is a special case of embed. */
    void add_dimension_with_stride(int s) {
        add_dimension();
        buf.dim[buf.dimensions-1].stride = s;
    }

    /** Methods for managing any GPU allocation. */
    // @{
    void set_host_dirty(bool v = true) {
        assert((!v || !device_dirty()) && "Cannot set host dirty when device is already dirty.");
        buf.set_host_dirty(v);
    }

    bool device_dirty() const {
        return buf.device_dirty();
    }

    bool host_dirty() const {
        return buf.host_dirty();
    }

    void set_device_dirty(bool v = true) {
        assert((!v || !host_dirty()) && "Cannot set device dirty when host is already dirty.");
        buf.set_device_dirty(v);
    }

    int copy_to_host(void *ctx = nullptr) {
        if (device_dirty()) {
            return buf.device_interface->copy_to_host(ctx, &buf);
        }
        return 0;
    }

    int copy_to_device(const struct halide_device_interface_t *device_interface, void *ctx = nullptr) {
        if (host_dirty()) {
            return device_interface->copy_to_device(ctx, &buf, device_interface);
        }
        return 0;
    }

    int device_malloc(const struct halide_device_interface_t *device_interface, void *ctx = nullptr) {
        return device_interface->device_malloc(ctx, &buf, device_interface);
    }

    int device_free(void *ctx = nullptr) {
        if (dev_ref_count) {
            assert(dev_ref_count->ownership == BufferDeviceOwnership::Allocated &&
                   "Can't call device_free on an unmanaged or wrapped native device handle. "
                   "Free the source allocation or call device_detach_native instead.");
            // Multiple people may be holding onto this dev field
            assert(dev_ref_count->count == 1 &&
                   "Multiple Halide::Runtime::Buffer objects share this device "
                   "allocation. Freeing it would create dangling references. "
                   "Don't call device_free on Halide buffers that you have copied or "
                   "passed by value.");
        }
        int ret = 0;
        if (buf.device_interface) {
            ret = buf.device_interface->device_free(ctx, &buf);
        }
        if (dev_ref_count) {
            delete dev_ref_count;
            dev_ref_count = nullptr;
        }
        return ret;
    }

    int device_wrap_native(const struct halide_device_interface_t *device_interface,
                           uint64_t handle, void *ctx = nullptr) {
        assert(device_interface);
        dev_ref_count = new DeviceRefCount;
        dev_ref_count->ownership = BufferDeviceOwnership::WrappedNative;
        return device_interface->wrap_native(ctx, &buf, handle, device_interface);
    }

    int device_detach_native(void *ctx = nullptr) {
        assert(dev_ref_count &&
               dev_ref_count->ownership == BufferDeviceOwnership::WrappedNative &&
               "Only call device_detach_native on buffers wrapping a native "
               "device handle via device_wrap_native. This buffer was allocated "
               "using device_malloc, or is unmanaged. "
               "Call device_free or free the original allocation instead.");
        // Multiple people may be holding onto this dev field
        assert(dev_ref_count->count == 1 &&
               "Multiple Halide::Runtime::Buffer objects share this device "
               "allocation. Freeing it could create dangling references. "
               "Don't call device_detach_native on Halide buffers that you "
               "have copied or passed by value.");
        int ret = 0;
        if (buf.device_interface) {
            ret = buf.device_interface->detach_native(ctx, &buf);
        }
        delete dev_ref_count;
        dev_ref_count = nullptr;
        return ret;
    }

    int device_and_host_malloc(const struct halide_device_interface_t *device_interface, void *ctx = nullptr) {
        return device_interface->device_and_host_malloc(ctx, &buf, device_interface);
    }

    int device_and_host_free(const struct halide_device_interface_t *device_interface, void *ctx = nullptr) {
        if (dev_ref_count) {
            assert(dev_ref_count->ownership == BufferDeviceOwnership::AllocatedDeviceAndHost &&
                   "Can't call device_and_host_free on a device handle not allocated with device_and_host_malloc. "
                   "Free the source allocation or call device_detach_native instead.");
            // Multiple people may be holding onto this dev field
            assert(dev_ref_count->count == 1 &&
                   "Multiple Halide::Runtime::Buffer objects share this device "
                   "allocation. Freeing it would create dangling references. "
                   "Don't call device_and_host_free on Halide buffers that you have copied or "
                   "passed by value.");
        }
        int ret = 0;
        if (buf.device_interface) {
            ret = buf.device_interface->device_and_host_free(ctx, &buf);
        }
        if (dev_ref_count) {
            delete dev_ref_count;
            dev_ref_count = nullptr;
        }
        return ret;
    }

    int device_sync(void *ctx = nullptr) {
        if (buf.device_interface) {
            return buf.device_interface->device_sync(ctx, &buf);
        } else {
            return 0;
        }
    }

    bool has_device_allocation() const {
        return buf.device != 0;
    }

    /** Return the method by which the device field is managed. */
    BufferDeviceOwnership device_ownership() const {
        if (dev_ref_count == nullptr) {
            return BufferDeviceOwnership::Allocated;
        }
        return dev_ref_count->ownership;
    }
    // @}

    /** If you use the (x, y, c) indexing convention, then Halide
     * Buffers are stored planar by default. This function constructs
     * an interleaved RGB or RGBA image that can still be indexed
     * using (x, y, c). Passing it to a generator requires that the
     * generator has been compiled with support for interleaved (also
     * known as packed or chunky) memory layouts. */
    static Buffer<void, D> make_interleaved(halide_type_t t, int width, int height, int channels) {
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
        Buffer<T, D> im(channels, width, height);
        im.transpose(0, 1);
        im.transpose(1, 2);
        return im;
    }

    /** Wrap an existing interleaved image. */
    static Buffer<add_const_if_T_is_const<void>, D>
    make_interleaved(halide_type_t t, T *data, int width, int height, int channels) {
        Buffer<add_const_if_T_is_const<void>, D> im(t, data, channels, width, height);
        im.transpose(0, 1);
        im.transpose(1, 2);
        return im;
    }

    /** Wrap an existing interleaved image. */
    static Buffer<T, D> make_interleaved(T *data, int width, int height, int channels) {
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

    /** Make a buffer with the same shape and memory nesting order as
     * another buffer. It may have a different type. */
    template<typename T2, int D2>
    static Buffer<T, D> make_with_shape_of(Buffer<T2, D2> src,
                                           void *(*allocate_fn)(size_t) = nullptr,
                                           void (*deallocate_fn)(void *) = nullptr) {
        // Reorder the dimensions of src to have strides in increasing order
        std::vector<int> swaps;
        for (int i = src.dimensions()-1; i > 0; i--) {
            for (int j = i; j > 0; j--) {
                if (src.dim(j-1).stride() > src.dim(j).stride()) {
                    src.transpose(j-1, j);
                    swaps.push_back(j);
                }
            }
        }

        // Rewrite the strides to be dense (this messes up src, which
        // is why we took it by value).
        halide_dimension_t *shape = src.buf.dim;
        for (int i = 0; i < src.dimensions(); i++) {
            if (i == 0) {
                shape[i].stride = 1;
            } else {
                shape[i].stride = shape[i-1].extent * shape[i-1].stride;
            }
        }

        // Undo the dimension reordering
        while (!swaps.empty()) {
            int j = swaps.back();
            std::swap(shape[j-1], shape[j]);
            swaps.pop_back();
        }

        // Use an explicit runtime type, and make dst a Buffer<void>, to allow
        // using this method with Buffer<void> for either src or dst.
        const halide_type_t dst_type = T_is_void
            ? src.type()
            : halide_type_of<typename std::remove_cv<not_void_T>::type>();
        Buffer<> dst(dst_type, nullptr, src.dimensions(), shape);
        dst.allocate(allocate_fn, deallocate_fn);

        return dst;
    }

private:

    template<typename ...Args>
    HALIDE_ALWAYS_INLINE
    ptrdiff_t offset_of(int d, int first, Args... rest) const {
        return offset_of(d+1, rest...) + this->buf.dim[d].stride * (first - this->buf.dim[d].min);
    }

    HALIDE_ALWAYS_INLINE
    ptrdiff_t offset_of(int d) const {
        return 0;
    }

    template<typename ...Args>
    HALIDE_ALWAYS_INLINE
    storage_T *address_of(Args... args) const {
        if (T_is_void) {
            return (storage_T *)(this->buf.host) + offset_of(0, args...) * type().bytes();
        } else {
            return (storage_T *)(this->buf.host) + offset_of(0, args...);
        }
    }

    HALIDE_ALWAYS_INLINE
    ptrdiff_t offset_of(const int *pos) const {
        ptrdiff_t offset = 0;
        for (int i = this->dimensions() - 1; i >= 0; i--) {
            offset += this->buf.dim[i].stride * (pos[i] - this->buf.dim[i].min);
        }
        return offset;
    }

    HALIDE_ALWAYS_INLINE
    storage_T *address_of(const int *pos) const {
        if (T_is_void) {
            return (storage_T *)this->buf.host + offset_of(pos) * type().bytes();
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
    HALIDE_ALWAYS_INLINE
    const not_void_T &operator()(int first, Args... rest) const {
        static_assert(!T_is_void,
                      "Cannot use operator() on Buffer<void> types");
        assert(!device_dirty());
        return *((const not_void_T *)(address_of(first, rest...)));
    }

    HALIDE_ALWAYS_INLINE
    const not_void_T &
    operator()() const {
        static_assert(!T_is_void,
                      "Cannot use operator() on Buffer<void> types");
        assert(!device_dirty());
        return *((const not_void_T *)(data()));
    }

    HALIDE_ALWAYS_INLINE
    const not_void_T &
    operator()(const int *pos) const {
        static_assert(!T_is_void,
                      "Cannot use operator() on Buffer<void> types");
        assert(!device_dirty());
        return *((const not_void_T *)(address_of(pos)));
    }

    template<typename ...Args,
             typename = typename std::enable_if<AllInts<Args...>::value>::type>
    HALIDE_ALWAYS_INLINE
    not_void_T &operator()(int first, Args... rest) {
        static_assert(!T_is_void,
                      "Cannot use operator() on Buffer<void> types");
        set_host_dirty();
        return *((not_void_T *)(address_of(first, rest...)));
    }

    HALIDE_ALWAYS_INLINE
    not_void_T &
    operator()() {
        static_assert(!T_is_void,
                      "Cannot use operator() on Buffer<void> types");
        set_host_dirty();
        return *((not_void_T *)(data()));
    }

    HALIDE_ALWAYS_INLINE
    not_void_T &
    operator()(const int *pos) {
        static_assert(!T_is_void,
                      "Cannot use operator() on Buffer<void> types");
        set_host_dirty();
        return *((not_void_T *)(address_of(pos)));
    }
    // @}

    /** Tests that all values in this buffer are equal to val. */
    bool all_equal(not_void_T val) const{
        bool all_equal = true;
        for_each_element([&](const int *pos) {all_equal &= (*this)(pos) == val;});
        return all_equal;
    }

    void fill(not_void_T val) {
        set_host_dirty();
        for_each_value([=](T &v) {v = val;});
    }

private:
    /** Helper functions for for_each_value. */
    // @{
    template<int N>
    struct for_each_value_task_dim {
        int extent;
        int stride[N];
    };

    // Given an array of strides, and a bunch of pointers to pointers
    // (all of different types), advance the pointers using the
    // strides.
    template<typename Ptr, typename ...Ptrs>
    static void advance_ptrs(const int *stride, Ptr *ptr, Ptrs... ptrs) {
        (*ptr) += *stride;
        advance_ptrs(stride + 1, ptrs...);
    }

    static void advance_ptrs(const int *) {}

    // Same as the above, but just increments the pointers.
    template<typename Ptr, typename ...Ptrs>
    static void increment_ptrs(Ptr *ptr, Ptrs... ptrs) {
        (*ptr)++;
        increment_ptrs(ptrs...);
    }

    static void increment_ptrs() {}

    // Given a bunch of pointers to buffers of different types, read
    // out their strides in the d'th dimension, and assert that their
    // sizes match in that dimension.
    template<typename T2, int D2, typename ...Args>
    void extract_strides(int d, int *strides, const Buffer<T2, D2> *first, Args... rest) {
        assert(first->dimensions() == dimensions());
        assert(first->dim(d).min() == dim(d).min() &&
               first->dim(d).max() == dim(d).max());
        *strides++ = first->dim(d).stride();
        extract_strides(d, strides, rest...);
    }

    void extract_strides(int d, int *strides) {}

    // The template function that constructs the loop nest for for_each_value
    template<int d, bool innermost_strides_are_one, typename Fn, typename... Ptrs>
    static void for_each_value_helper(Fn &&f, const for_each_value_task_dim<sizeof...(Ptrs)> *t, Ptrs... ptrs) {
        if (d == -1) {
            f((*ptrs)...);
        } else {
            for (int i = t[d].extent; i != 0; i--) {
                for_each_value_helper<(d >= 0 ? d - 1 : -1), innermost_strides_are_one>(f, t, ptrs...);
                if (d == 0 && innermost_strides_are_one) {
                    // It helps with auto-vectorization to statically
                    // know the addresses are one apart in memory.
                    increment_ptrs((&ptrs)...);
                } else {
                    advance_ptrs(t[d].stride, (&ptrs)...);
                }
            }
        }
    }

    template<bool innermost_strides_are_one, typename Fn, typename... Ptrs>
    static void for_each_value_helper(Fn &&f, int d, const for_each_value_task_dim<sizeof...(Ptrs)> *t, Ptrs... ptrs) {
        // When we hit a low dimensionality, switch from runtime
        // recursion to template recursion.
        if (d == -1) {
            for_each_value_helper<-1, innermost_strides_are_one>(f, t, ptrs...);
        } else if (d == 0) {
            for_each_value_helper<0, innermost_strides_are_one>(f, t, ptrs...);
        } else if (d == 1) {
            for_each_value_helper<1, innermost_strides_are_one>(f, t, ptrs...);
        } else if (d == 2) {
            for_each_value_helper<2, innermost_strides_are_one>(f, t, ptrs...);
        } else {
            for (int i = t[d].extent; i != 0; i--) {
                for_each_value_helper<innermost_strides_are_one>(f, d-1, t, ptrs...);
                advance_ptrs(t[d].stride, (&ptrs)...);
            }
        }
    }
    // @}

public:
    /** Call a function on every value in the buffer, and the
     * corresponding values in some number of other buffers of the
     * same size. The function should take a reference, const
     * reference, or value of the correct type for each buffer. This
     * effectively lifts a function of scalars to an element-wise
     * function of buffers. This produces code that the compiler can
     * autovectorize. This is slightly cheaper than for_each_element,
     * because it does not need to track the coordinates. */
    template<typename Fn, typename ...Args, int N = sizeof...(Args) + 1>
    void for_each_value(Fn &&f, Args... other_buffers) {
        for_each_value_task_dim<N> *t =
            (for_each_value_task_dim<N> *)HALIDE_ALLOCA((dimensions()+1) * sizeof(for_each_value_task_dim<N>));
        for (int i = 0; i <= dimensions(); i++) {
            for (int j = 0; j < N; j++) {
                t[i].stride[j] = 0;
            }
            t[i].extent = 1;
        }

        for (int i = 0; i < dimensions(); i++) {
            extract_strides(i, t[i].stride, this, &other_buffers...);
            t[i].extent = dim(i).extent();
            // Order the dimensions by stride, so that the traversal is cache-coherent.
            for (int j = i; j > 0 && t[j].stride[0] < t[j-1].stride[0]; j--) {
                std::swap(t[j], t[j-1]);
            }
        }

        // flatten dimensions where possible to make a larger inner
        // loop for autovectorization.
        int d = dimensions();
        for (int i = 1; i < d; i++) {
            bool flat = true;
            for (int j = 0; j < N; j++) {
                flat = flat && t[i-1].stride[j] * t[i-1].extent == t[i].stride[j];
            }
            if (flat) {
                t[i-1].extent *= t[i].extent;
                for (int j = i; j < dimensions(); j++) {
                    t[j] = t[j+1];
                }
                i--;
                d--;
            }
        }

        bool innermost_strides_are_one = false;
        if (dimensions() > 0) {
            innermost_strides_are_one = true;
            for (int j = 0; j < N; j++) {
                innermost_strides_are_one &= t[0].stride[j] == 1;
            }
        }

        if (innermost_strides_are_one) {
            for_each_value_helper<true>(f, dimensions() - 1, t, begin(), (other_buffers.begin())...);
        } else {
            for_each_value_helper<false>(f, dimensions() - 1, t, begin(), (other_buffers.begin())...);
        }
    }

private:

    // Helper functions for for_each_element
    struct for_each_element_task_dim {
        int min, max;
    };

    /** If f is callable with this many args, call it. The first
     * argument is just to make the overloads distinct. Actual
     * overload selection is done using the enable_if. */
    template<typename Fn,
             typename ...Args,
             typename = decltype(std::declval<Fn>()(std::declval<Args>()...))>
    HALIDE_ALWAYS_INLINE
    static void for_each_element_variadic(int, int, const for_each_element_task_dim *, Fn &&f, Args... args) {
        f(args...);
    }

    /** If the above overload is impossible, we add an outer loop over
     * an additional argument and try again. */
    template<typename Fn,
             typename ...Args>
    HALIDE_ALWAYS_INLINE
    static void for_each_element_variadic(double, int d, const for_each_element_task_dim *t, Fn &&f, Args... args) {
        for (int i = t[d].min; i <= t[d].max; i++) {
            for_each_element_variadic(0, d - 1, t, std::forward<Fn>(f), i, args...);
        }
    }

    /** Determine the minimum number of arguments a callable can take
     * using the same trick. */
    template<typename Fn,
             typename ...Args,
             typename = decltype(std::declval<Fn>()(std::declval<Args>()...))>
    HALIDE_ALWAYS_INLINE
    static int num_args(int, Fn &&, Args...) {
        return (int)(sizeof...(Args));
    }

    /** The recursive version is only enabled up to a recursion limit
     * of 256. This catches callables that aren't callable with any
     * number of ints. */
    template<typename Fn,
             typename ...Args>
    HALIDE_ALWAYS_INLINE
    static int num_args(double, Fn &&f, Args... args) {
        static_assert(sizeof...(args) <= 256,
                      "Callable passed to for_each_element must accept either a const int *,"
                      " or up to 256 ints. No such operator found. Expect infinite template recursion.");
        return num_args(0, std::forward<Fn>(f), 0, args...);
    }

    /** A version where the callable takes a position array instead,
     * with compile-time recursion on the dimensionality.  This
     * overload is preferred to the one below using the same int vs
     * double trick as above, but is impossible once d hits -1 using
     * std::enable_if. */
    template<int d,
             typename Fn,
             typename = typename std::enable_if<(d >= 0)>::type>
    HALIDE_ALWAYS_INLINE
    static void for_each_element_array_helper(int, const for_each_element_task_dim *t, Fn &&f, int *pos) {
        for (pos[d] = t[d].min; pos[d] <= t[d].max; pos[d]++) {
            for_each_element_array_helper<d - 1>(0, t, std::forward<Fn>(f), pos);
        }
    }

    /** Base case for recursion above. */
    template<int d,
             typename Fn,
             typename = typename std::enable_if<(d < 0)>::type>
    HALIDE_ALWAYS_INLINE
    static void for_each_element_array_helper(double, const for_each_element_task_dim *t, Fn &&f, int *pos) {
        f(pos);
    }

    /** A run-time-recursive version (instead of
     * compile-time-recursive) that requires the callable to take a
     * pointer to a position array instead. Dispatches to the
     * compile-time-recursive version once the dimensionality gets
     * small. */
    template<typename Fn>
    static void for_each_element_array(int d, const for_each_element_task_dim *t, Fn &&f, int *pos) {
        if (d == -1) {
            f(pos);
        } else if (d == 0) {
            // Once the dimensionality gets small enough, dispatch to
            // a compile-time-recursive version for better codegen of
            // the inner loops.
            for_each_element_array_helper<0, Fn>(0, t, std::forward<Fn>(f), pos);
        } else if (d == 1) {
            for_each_element_array_helper<1, Fn>(0, t, std::forward<Fn>(f), pos);
        } else if (d == 2) {
            for_each_element_array_helper<2, Fn>(0, t, std::forward<Fn>(f), pos);
        } else if (d == 3) {
            for_each_element_array_helper<3, Fn>(0, t, std::forward<Fn>(f), pos);
        } else {
            for (pos[d] = t[d].min; pos[d] <= t[d].max; pos[d]++) {
                for_each_element_array(d - 1, t, std::forward<Fn>(f), pos);
            }
        }
    }

    /** We now have two overloads for for_each_element. This one
     * triggers if the callable takes a const int *.
     */
    template<typename Fn,
             typename = decltype(std::declval<Fn>()((const int *)nullptr))>
    static void for_each_element(int, int dims, const for_each_element_task_dim *t, Fn &&f, int check = 0) {
        int *pos = (int *)HALIDE_ALLOCA(dims * sizeof(int));
        for_each_element_array(dims - 1, t, std::forward<Fn>(f), pos);
    }

    /** This one triggers otherwise. It treats the callable as
     * something that takes some number of ints. */
    template<typename Fn>
    HALIDE_ALWAYS_INLINE
    static void for_each_element(double, int dims, const for_each_element_task_dim *t, Fn &&f) {
        int args = num_args(0, std::forward<Fn>(f));
        assert(dims >= args);
        for_each_element_variadic(0, args - 1, t, std::forward<Fn>(f));
    }
public:

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
     im.for_each_element([&](int x, int y) {
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
     im.for_each_element([&](const int *pos) {im(pos) = pos[0];});
     \endcode

     * It is also possible to use for_each_element to iterate over entire
     * rows or columns by cropping the buffer to a single column or row
     * respectively and iterating over elements of the result. For example,
     * to set the diagonal of the image to 1 by iterating over the columns:

     \code
     Buffer<float, 3> im(100, 100, 3);
         im.sliced(1, 0).for_each_element([&](int x, int c) {
         im(x, x, c) = 1.0f;
     });
     \endcode

     * Or, assuming the memory layout is known to be dense per row, one can
     * memset each row of an image like so:

     \code
     Buffer<float, 3> im(100, 100, 3);
     im.sliced(0, 0).for_each_element([&](int y, int c) {
         memset(&im(0, y, c), 0, sizeof(float) * im.width());
     });
     \endcode

    */
    template<typename Fn>
    void for_each_element(Fn &&f) const {
        for_each_element_task_dim *t =
            (for_each_element_task_dim *)HALIDE_ALLOCA(dimensions() * sizeof(for_each_element_task_dim));
        for (int i = 0; i < dimensions(); i++) {
            t[i].min = dim(i).min();
            t[i].max = dim(i).max();
        }
        for_each_element(0, dimensions(), t, std::forward<Fn>(f));
    }

private:
    template<typename Fn>
    struct FillHelper {
        Fn f;
        Buffer<T, D> *buf;

        template<typename... Args,
                 typename = decltype(std::declval<Fn>()(std::declval<Args>()...))>
        void operator()(Args... args) {
            (*buf)(args...) = f(args...);
        }

        FillHelper(Fn &&f, Buffer<T, D> *buf) : f(std::forward<Fn>(f)), buf(buf) {}
    };

public:
    /** Fill a buffer by evaluating a callable at every site. The
     * callable should look much like a callable passed to
     * for_each_element, but it should return the value that should be
     * stored to the coordinate corresponding to the arguments. */
    template<typename Fn,
             typename = typename std::enable_if<!std::is_arithmetic<typename std::decay<Fn>::type>::value>::type>
    void fill(Fn &&f) {
        // We'll go via for_each_element. We need a variadic wrapper lambda.
        FillHelper<Fn> wrapper(std::forward<Fn>(f), this);
        for_each_element(wrapper);
    }

    /** Check if an input buffer passed extern stage is a querying
     * bounds. Compared to doing the host pointer check directly,
     * this both adds clarity to code and will facilitate moving to
     * another representation for bounds query arguments. */
    bool is_bounds_query() {
        return buf.is_bounds_query();
    }

};

}  // namespace Runtime
}  // namespace Halide

#undef HALIDE_ALLOCA

#endif  // HALIDE_RUNTIME_IMAGE_H

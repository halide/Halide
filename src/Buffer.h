#ifndef HALIDE_BUFFER_H
#define HALIDE_BUFFER_H

/** \file
 * Defines Buffer - A c++ wrapper around a buffer_t.
 */

#include <stdint.h>

#include "runtime/HalideRuntime.h" // For buffer_t
#include "IntrusivePtr.h"
#include "Type.h"
#include "Argument.h"

namespace Halide {
namespace Internal {
struct BufferContents;
struct JITModule;
}

/** The internal representation of an image, or other dense array
 * data. The Image type provides a typed view onto a buffer for the
 * purposes of direct manipulation. A buffer may be stored in main
 * memory, or some other memory space (e.g. a gpu). If you want to use
 * this as an Image, see the Image class. Casting a Buffer to an Image
 * will do any appropriate copy-back. This class is a fairly thin
 * wrapper on a buffer_t, which is the C-style type Halide uses for
 * passing buffers around.
 */
class Buffer {
private:
    Internal::IntrusivePtr<Internal::BufferContents> contents;

public:
    Buffer() : contents(NULL) {}

    EXPORT Buffer(Type t, int x_size = 0, int y_size = 0, int z_size = 0, int w_size = 0,
                  uint8_t* data = NULL, const std::string &name = "");

    EXPORT Buffer(Type t, const std::vector<int32_t> &sizes,
                  uint8_t* data = NULL, const std::string &name = "");

    EXPORT Buffer(Type t, const buffer_t *buf, const std::string &name = "");

    /** Get a pointer to the host-side memory. */
    EXPORT void *host_ptr() const;

    /** Get a pointer to the raw buffer_t struct that this class wraps. */
    EXPORT buffer_t *raw_buffer() const;

    /** Get the device-side pointer/handle for this buffer. Will be
     * zero if no device was involved in the creation of this
     * buffer. */
    EXPORT uint64_t device_handle() const;

    /** Has this buffer been modified on the cpu since last copied to a
     * device. Not meaningful unless there's a device involved. */
    EXPORT bool host_dirty() const;

    /** Let Halide know that the host-side memory backing this buffer
     * has been externally modified. You shouldn't normally need to
     * call this, because it is done for you when you cast a Buffer to
     * an Image in order to modify it. */
    EXPORT void set_host_dirty(bool dirty = true);

    /** Has this buffer been modified on device since last copied to
     * the cpu. Not meaninful unless there's a device involved. */
    EXPORT bool device_dirty() const;

    /** Let Halide know that the device-side memory backing this
     * buffer has been externally modified, and so the cpu-side memory
     * is invalid. A copy-back will occur the next time you cast this
     * Buffer to an Image, or the next time this buffer is accessed on
     * the host in a halide pipeline. */
    EXPORT void set_device_dirty(bool dirty = true);

    /** Get the dimensionality of this buffer. Uses the convention
     * that the extent field of a buffer_t should contain zero when
     * the dimensions end. */
    EXPORT int dimensions() const;

    /** Get the extent of this buffer in the given dimension. */
    EXPORT int extent(int dim) const;

    /** Get the number of bytes between adjacent elements of this buffer along the given dimension. */
    EXPORT int stride(int dim) const;

    /** Get the coordinate in the function that this buffer represents
     * that corresponds to the base address of the buffer. */
    EXPORT int min(int dim) const;

    /** Set the coordinate in the function that this buffer represents
     * that corresponds to the base address of the buffer. */
    EXPORT void set_min(int m0, int m1 = 0, int m2 = 0, int m3 = 0);

    /** Get the Halide type of the contents of this buffer. */
    EXPORT Type type() const;

    /** Compare two buffers for identity (not equality of data). */
    EXPORT bool same_as(const Buffer &other) const;

    /** Check if this buffer handle actually points to data. */
    EXPORT bool defined() const;

    /** Get the runtime name of this buffer used for debugging. */
    EXPORT const std::string &name() const;

    /** Convert this buffer to an argument to a halide pipeline. */
    EXPORT operator Argument() const;

    /** If this buffer was created *on-device* by a jit-compiled
     * realization, then copy it back to the cpu-side memory. This is
     * usually achieved by casting the Buffer to an Image. */
    EXPORT int copy_to_host();

    /** If this buffer was created by a jit-compiled realization on a
     * device-aware target (e.g. PTX), then copy the cpu-side data to
     * the device-side allocation. TODO: I believe this currently
     * aborts messily if no device-side allocation exists. You might
     * think you want to do this because you've modified the data
     * manually on the host before calling another Halide pipeline,
     * but what you actually want to do in that situation is set the
     * host_dirty bit so that Halide can manage the copy lazily for
     * you. Casting the Buffer to an Image sets the dirty bit for
     * you. */
    EXPORT int copy_to_device();

    /** If this buffer was created by a jit-compiled realization on a
     * device-aware target (e.g. PTX), then free the device-side
     * allocation, if there is one. Done automatically when the last
     * reference to this buffer dies. */
    EXPORT int free_dev_buffer();

};

}

#endif

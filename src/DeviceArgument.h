#ifndef HALIDE_DEVICE_ARGUMENT_H
#define HALIDE_DEVICE_ARGUMENT_H

/** \file
 * Defines helpers for passing arguments to separate devices, such as GPUs.
 */
#include <string>

#include "Closure.h"
#include "Expr.h"
#include "ModulusRemainder.h"

namespace Halide {
namespace Internal {

/** A DeviceArgument looks similar to an Halide::Argument, but has behavioral
 * differences that make it specific to the GPU pipeline; the fact that
 * neither is-a nor has-a Halide::Argument is deliberate. In particular, note
 * that a Halide::Argument that is a buffer can be read or write, but not both,
 * while a DeviceArgument that is a buffer can be read *and* write for some GPU
 * backends. */
struct DeviceArgument {
    /** The name of the argument */
    std::string name;

    /** An argument is either a primitive type (for parameters), or a
     * buffer pointer.
     *
     * If is_buffer == false, then type fully encodes the expected type
     * of the scalar argument.
     *
     * If is_buffer == true, then type.bytes() should be used to determine
     * elem_size of the buffer; additionally, type.code *should* reflect
     * the expected interpretation of the buffer data (e.g. float vs int),
     * but there is no runtime enforcement of this at present.
     */
    bool is_buffer = false;

    /** If is_buffer == true and memory_type == GPUTexture, this argument should be
     * passed and accessed through texture sampler operations instead of
     * directly as a memory array
     */
    MemoryType memory_type = MemoryType::Auto;

    /** If is_buffer is true, this is the dimensionality of the buffer.
     * If is_buffer is false, this value is ignored (and should always be set to zero) */
    uint8_t dimensions = 0;

    /** If this is a scalar parameter, then this is its type.
     *
     * If this is a buffer parameter, this is used to determine elem_size
     * of the halide_buffer_t.
     *
     * Note that type.lanes() should always be 1 here. */
    Type type;

    /** The static size of the argument if known, or zero otherwise. */
    size_t size = 0;

    /** The index of the first element of the argument when packed into a wider
     * type, such as packing scalar floats into vec4 for GLSL. */
    size_t packed_index = 0;

    /** For buffers, these two variables can be used to specify whether the
     * buffer is read or written. By default, we assume that the argument
     * buffer is read-write and set both flags. */
    bool read = false;
    bool write = false;

    /** Alignment information for integer parameters. */
    ModulusRemainder alignment;

    DeviceArgument() = default;

    DeviceArgument(const std::string &_name,
                   bool _is_buffer,
                   MemoryType _mem,
                   Type _type,
                   uint8_t _dimensions,
                   size_t _size = 0)
        : name(_name),
          is_buffer(_is_buffer),
          memory_type(_mem),
          dimensions(_dimensions),
          type(_type),
          size(_size),

          read(_is_buffer),
          write(_is_buffer) {
    }
};

/** A Closure modified to inspect GPU-specific memory accesses, and
 * produce a vector of DeviceArgument objects. */
class HostClosure : public Closure {
public:
    HostClosure() = default;

    /** Get a description of the captured arguments. */
    std::vector<DeviceArgument> arguments();

protected:
    using Internal::Closure::visit;

    void visit(const For *loop) override;
    void visit(const Call *op) override;
};

}  // namespace Internal
}  // namespace Halide

#endif

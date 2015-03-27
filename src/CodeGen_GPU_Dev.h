#ifndef HALIDE_CODEGEN_GPU_DEV_H
#define HALIDE_CODEGEN_GPU_DEV_H

/** \file
 * Defines the code-generator interface for producing GPU device code
 */

#include "IR.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** A GPU_Argument looks similar to an Halide::Argument, but has behavioral
 * differences that make it specific to the GPU pipeline; the fact that
 * neither is-a nor has-a Halide::Argument is deliberate. In particular, note
 * that a Halide::Argument that is a buffer can be read or write, but not both,
 * while a GPU_Argument that is a buffer can be read *and* write for some GPU
 * backends. */
struct GPU_Argument {
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
    bool is_buffer;

    /** If is_buffer is true, this is the dimensionality of the buffer.
     * If is_buffer is false, this value is ignored (and should always be set to zero) */
    uint8_t dimensions;

    /** If this is a scalar parameter, then this is its type.
     *
     * If this is a buffer parameter, this is used to determine elem_size
     * of the buffer_t.
     *
     * Note that type.width should always be 1 here. */
    Type type;

    /** The static size of the argument if known, or zero otherwise. */
    size_t size;

    /** The index of the first element of the argument when packed into a wider
     * type, such as packing scalar floats into vec4 for GLSL. */
    size_t packed_index;

    /** For buffers, these two variables can be used to specify whether the
     * buffer is read or written. By default, we assume that the argument
     * buffer is read-write and set both flags. */
    bool read;
    bool write;

    GPU_Argument() :
        is_buffer(false),
        dimensions(0),
        size(0),
        packed_index(0),
        read(false),
        write(false) {}

    GPU_Argument(const std::string &_name,
                 bool _is_buffer,
                 Type _type,
                 uint8_t _dimensions,
                 size_t _size = 0) :
        name(_name),
        is_buffer(_is_buffer),
        dimensions(_dimensions),
        type(_type),
        size(_size),
        packed_index(0),
        read(_is_buffer),
        write(_is_buffer) {}
};

/** A code generator that emits GPU code from a given Halide stmt. */
struct CodeGen_GPU_Dev {
    virtual ~CodeGen_GPU_Dev();

    /** Compile a GPU kernel into the module. This may be called many times
     * with different kernels, which will all be accumulated into a single
     * source module shared by a given Halide pipeline. */
    virtual void add_kernel(Stmt stmt,
                            const std::string &name,
                            const std::vector<GPU_Argument> &args) = 0;

    /** (Re)initialize the GPU kernel module. This is separate from compile,
     * since a GPU device module will often have many kernels compiled into it
     * for a single pipeline. */
    virtual void init_module() = 0;

    virtual std::vector<char> compile_to_src() = 0;

    virtual std::string get_current_kernel_name() = 0;

    virtual void dump() = 0;

    /** This routine returns the GPU API name that is combined into
     *  runtime routine names to ensure each GPU API has a unique
     *  name.
     */
    virtual std::string api_unique_name() = 0;

    /** Returns the specified name transformed by the variable naming rules
     * for the GPU language backend. Used to determine the name of a parameter
     * during host codegen. */
    virtual std::string print_gpu_name(const std::string &name) = 0;

    static bool is_gpu_var(const std::string &name);
    static bool is_gpu_block_var(const std::string &name);
    static bool is_gpu_thread_var(const std::string &name);

    /** Checks if expr is block uniform, i.e. does not depend on a thread
     * var. */
    static bool is_block_uniform(Expr expr);
    /** Checks if the buffer is a candidate for constant storage. Most
     * GPUs (APIs) support a constant memory storage class that cannot be
     * written to and performs well for block uniform accesses. A buffer is a
     * candidate for constant storage if it is never written to, and loads are
     * uniform within the workgroup. */
    static bool is_buffer_constant(Stmt kernel, const std::string &buffer);
};

}}

#endif

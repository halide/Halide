#ifndef HALIDE_CODEGEN_GPU_DEV_H
#define HALIDE_CODEGEN_GPU_DEV_H

/** \file
 * Defines the code-generator interface for producing GPU device code
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** A bit more information attached to an argument useful for GPU backends. */
struct GPU_Argument : public Argument {
    /** The static size of the argument if known, or zero otherwise. */
    size_t size;

    GPU_Argument() : size(0) {}
    GPU_Argument(const std::string &_name, bool _is_buffer, Type _type) :
        Argument(_name, _is_buffer, _type), size(0) {}
    GPU_Argument(const std::string &_name, bool _is_buffer, Type _type,
                 size_t _size) :
        Argument(_name, _is_buffer, _type), size(_size) {}
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
    std::string api_unique_name() = 0;

    static bool is_gpu_var(const std::string &name);
    static bool is_gpu_block_var(const std::string &name);
    static bool is_gpu_thread_var(const std::string &name);

    /** Checks if expr is block uniform, i.e. does not depend on a thread
     * var. */
    static bool is_block_uniform(Expr expr);
    /** Checks if the buffer is a candidate for constant storage. Most
     * GPUs (APIs) support a constant memory storage class that cannot be
     * written to and performs well for block uniform accesses. */
    static bool is_buffer_constant(Stmt kernel, const std::string &buffer);
};

}}

#endif

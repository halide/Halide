#ifndef HALIDE_CODEGEN_GPU_DEV_H
#define HALIDE_CODEGEN_GPU_DEV_H

/** \file
 * Defines the code-generator interface for producing GPU device code
 */

#include "IR.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** A bit more information attached to an argument useful for GPU backends. */
struct GPU_Argument : public Argument {
    /** The static size of the argument if known, or zero otherwise. */
    size_t size;

    /** The index of the first element of the argument when packed into a wider
     * type, such as packing scalar floats into vec4 for GLSL. */
    size_t packed_index;

    GPU_Argument() : size(0), packed_index(0) {}
    GPU_Argument(const std::string &_name, bool _is_buffer, Type _type) :
        Argument(_name, _is_buffer, _type), size(0), packed_index(0) {}
    GPU_Argument(const std::string &_name, bool _is_buffer, Type _type,
                 size_t _size) : 
        Argument(_name, _is_buffer, _type), size(_size), packed_index(0) {}
};

/** A code generator that emits GPU code from a given Halide stmt. */
struct CodeGen_GPU_Dev {
    virtual ~CodeGen_GPU_Dev();

    /** Create a new GPU code generator for the given target. */
    static CodeGen_GPU_Dev *new_for_target(Target);

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

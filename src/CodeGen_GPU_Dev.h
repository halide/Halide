#ifndef HALIDE_CODEGEN_GPU_DEV_H
#define HALIDE_CODEGEN_GPU_DEV_H

/** \file
 * Defines the code-generator interface for producing GPU device code
 */
#include <string>
#include <vector>

#include "CodeGen_C.h"
#include "DeviceArgument.h"
#include "Expr.h"

namespace Halide {
namespace Internal {

/** A code generator that emits GPU code from a given Halide stmt. */
struct CodeGen_GPU_Dev {
    virtual ~CodeGen_GPU_Dev();

    /** Compile a GPU kernel into the module. This may be called many times
     * with different kernels, which will all be accumulated into a single
     * source module shared by a given Halide pipeline. */
    virtual void add_kernel(Stmt stmt,
                            const std::string &name,
                            const std::vector<DeviceArgument> &args) = 0;

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

    /** Allows the GPU device specific code to request halide_type_t
     * values to be passed to the kernel_run routine rather than just
     * argument type sizes.
     */
    virtual bool kernel_run_takes_types() const {
        return false;
    }

    /** Checks if expr is block uniform, i.e. does not depend on a thread
     * var. */
    static bool is_block_uniform(const Expr &expr);
    /** Checks if the buffer is a candidate for constant storage. Most
     * GPUs (APIs) support a constant memory storage class that cannot be
     * written to and performs well for block uniform accesses. A buffer is a
     * candidate for constant storage if it is never written to, and loads are
     * uniform within the workgroup. */
    static bool is_buffer_constant(const Stmt &kernel, const std::string &buffer);

    /** Modifies predicated loads and stores to be non-predicated, since most
     * GPU backends do not support predication. */
    static Stmt scalarize_predicated_loads_stores(Stmt &s);

    /** An mask describing which type of memory fence to use for the gpu_thread_barrier()
     * intrinsic.  Not all GPUs APIs support all types.
     */
    enum MemoryFenceType {
        None = 0,    // No fence required (just a sync)
        Device = 1,  // Device/global memory fence
        Shared = 2   // Threadgroup/shared memory fence
    };
};

/** A base class for GPU backends that require C-like shader output.
 * GPU backends derive from and specialize this class. */
class CodeGen_GPU_C : public CodeGen_C {
public:
    /** OpenCL and WGSL use different syntax than C for immediate vectors. This
    enum defines which style should be used by the backend. */
    enum class VectorDeclarationStyle {
        CLikeSyntax = 0,
        OpenCLSyntax = 1,
        WGSLSyntax = 2,
    };

    CodeGen_GPU_C(std::ostream &s, Target t)
        : CodeGen_C(s, t) {
    }

protected:
    using CodeGen_C::visit;
    void visit(const Shuffle *op) override;
    void visit(const Call *op) override;

    std::string print_extern_call(const Call *op) override;

    VectorDeclarationStyle vector_declaration_style = VectorDeclarationStyle::CLikeSyntax;
};

}  // namespace Internal
}  // namespace Halide

#endif

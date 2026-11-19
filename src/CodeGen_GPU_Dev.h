#ifndef HALIDE_CODEGEN_GPU_DEV_H
#define HALIDE_CODEGEN_GPU_DEV_H

/** \file
 * Defines the code-generator interface for producing GPU device code
 */
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "CodeGen_C.h"
#include "DeviceArgument.h"
#include "Expr.h"

namespace Halide {
namespace Internal {

struct StmtSourceMap;
struct IRPrinterLocation;
struct DeviceCodeLines;

/** All IR nodes in a Stmt. */
std::set<const IRNode *> nodes_of(const Stmt &s);

/** Where each line of C-like device source came from: the innermost Stmt in
 * nodes (those of a Stmt that holds the kernels' Stmts) that it was printed
 * for, according to where the Stmts were printed in it (locations). If ir is
 * set, a printing of that Stmt, the source also gets #line directives that
 * say so, and lines made from nothing in it go back to their own line numbers
 * in source_name. */
DeviceCodeLines c_like_device_code_lines(const std::string &source, const std::vector<IRPrinterLocation> &locations,
                                         const std::set<const IRNode *> &nodes, const StmtSourceMap *ir,
                                         const std::string &source_name);

/** Whether a line of PTX is part of the line information in it: .loc and
 * .file directives, and the labels and sections that only it uses. */
bool is_ptx_line_info(std::string_view line);

/** A code generator that emits GPU code from a given Halide stmt. */
struct CodeGen_GPU_Dev {
    virtual ~CodeGen_GPU_Dev();

    /** Have the next module record where each line of its device code came
     * from in conceptual_stmt, a Stmt that holds the kernels' Stmts, for
     * device_code_lines(). If in_code, the device code says so itself too,
     * for drivers and debuggers, with the lines of conceptual_source, a
     * printing of conceptual_stmt. Code generators that need that printing
     * get it either way. Not all code generators do anything with this. */
    virtual void set_line_info(const Stmt &conceptual_stmt, const StmtSourceMap *conceptual_source, bool in_code) {
    }

    /** After compile_to_src(): the device code, with where its lines came
     * from, if set_line_info() was called. Its name is left empty. */
    virtual DeviceCodeLines device_code_lines();

    /** Compile a GPU kernel into the module. This may be called many times
     * with different kernels, which will all be accumulated into a single
     * source module shared by a given Halide pipeline. */
    virtual void add_kernel(Stmt stmt,
                            const std::string &name,
                            const std::vector<DeviceArgument> &args) = 0;

    /** Cap the registers a thread of the next kernel added may use. Zero, the
     * default, leaves it to the backend compiler. Only CUDA does anything with
     * this; the other APIs offer no equivalent and ignore it. */
    virtual void set_kernel_max_registers(int n) {
    }

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

    /** Return the physical scalar type passed to the kernel_run routine.
     * Backends may widen types that are not legal in their parameter storage.
     */
    virtual Type kernel_run_scalar_arg_type(Type type) const {
        return type;
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

    /** Some GPU APIs need to know what floating point mode we're in at kernel
     * emission time, to emit appropriate pragmas. */
    bool any_strict_float = false;

public:
    void set_any_strict_float(bool any_strict_float) {
        this->any_strict_float = any_strict_float;
    }
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
    void visit(const Mod *op) override;

    std::string print_extern_call(const Call *op) override;

    VectorDeclarationStyle vector_declaration_style = VectorDeclarationStyle::CLikeSyntax;
    bool abs_returns_unsigned_type{false};
};

}  // namespace Internal
}  // namespace Halide

#endif

#ifndef HALIDE_CODEGEN_D3D12_COMPUTE_DEV_H
#define HALIDE_CODEGEN_D3D12_COMPUTE_DEV_H

/** \file
 * Defines the code-generator for producing D3D12-compatible HLSL kernel code
 */

#include <sstream>

#include "CodeGen_C.h"
#include "CodeGen_GPU_Dev.h"
#include "Target.h"

namespace Halide {
namespace Internal {

class CodeGen_D3D12Compute_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_D3D12Compute_Dev(Target target);

    /** Compile a GPU kernel into the module. This may be called many times
     * with different kernels, which will all be accumulated into a single
     * source module shared by a given Halide pipeline. */
    void add_kernel(Stmt stmt,
                    const std::string &name,
                    const std::vector<DeviceArgument> &args);

    /** (Re)initialize the GPU kernel module. This is separate from compile,
     * since a GPU device module will often have many kernels compiled into it
     * for a single pipeline. */
    void init_module();

    std::vector<char> compile_to_src();

    std::string get_current_kernel_name();

    void dump();

    virtual std::string print_gpu_name(const std::string &name);

    std::string api_unique_name() { return "d3d12compute"; }

protected:

    class CodeGen_D3D12Compute_C : public CodeGen_C {
    public:
        CodeGen_D3D12Compute_C(std::ostream &s, Target t) : CodeGen_C(s, t) {}
        void add_kernel(Stmt stmt,
                        const std::string &name,
                        const std::vector<DeviceArgument> &args);

    protected:
        using CodeGen_C::visit;
        std::string print_type(Type type, AppendSpaceIfNeeded space_option = DoNotAppendSpace);
        std::string print_storage_type(Type type);
        std::string print_type_maybe_storage(Type type, bool storage, AppendSpaceIfNeeded space);
        std::string print_reinterpret(Type type, Expr e);
        std::string print_extern_call(const Call *op);

        std::string print_vanilla_cast(Type type, std::string value_expr);
        std::string print_reinforced_cast(Type type, std::string value_expr);
        std::string print_cast(Type target_type, Type source_type, std::string value_expr);
        std::string print_reinterpret_cast(Type type, std::string value_expr);

        virtual std::string print_assignment(Type t, const std::string &rhs);

        void visit(const Evaluate *op);

        void visit(const Min *);
        void visit(const Max *);
        void visit(const Div *);
        void visit(const Mod *);
        void visit(const For *);
        void visit(const Ramp *op);
        void visit(const Broadcast *op);
        void visit(const Load *op);
        void visit(const Store *op);
        void visit(const Select *op);
        void visit(const Allocate *op);
        void visit(const Free *op);
        void visit(const Cast *op);
    };

    std::ostringstream src_stream;
    std::string cur_kernel_name;
    CodeGen_D3D12Compute_C d3d12compute_c;
};

}}

#endif
